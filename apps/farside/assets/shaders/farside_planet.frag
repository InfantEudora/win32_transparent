#version 430 core
/*
    The bench's planets and moons: a surface worked out from noise on the sphere, lit by
    lighting.glsl exactly as default.frag lights a material - so the engine's sun, its shadow map
    and the ambient term all apply, and the night side is simply the side facing away.

    THE SURFACE TURNS WITH THE BODY because it is computed in the body's own frame: the fragment's
    world position, less the centre, rotated back by the body's inverse rotation. Not from the UVs -
    MakeSphere's pole is a fan of triangles whose u differs per triangle, so a direction rebuilt from
    uv tears along every edge of that fan, and the pole is the point that faces the camera.

    RELIEF: lighting.glsl shades with the smooth vertex normal and has no hook for another, so the
    noise's slopes are applied here as a factor on the albedo - the bumped normal's sunlight over
    the smooth one's. Same result on the lit side, nothing on the dark side.

    Up to MAX_BODIES bodies share the program; each fragment finds its own by which surface it lies
    on, so one sphere mesh serves every body.
*/
#include "texture_units.glsl"

layout (location = 0) out vec4 color;

//default.vert's varyings, at its locations - lighting.glsl reads them.
layout (location = 0)  in vec3 vposition;
layout (location = 1)  in vec3 vnormal;
layout (location = 2)  in vec2 vuv;
layout (location = 3)  in mat3 TBN;
layout (location = 6)  flat in int vmatindex;
layout (location = 7)  flat in int vobjid;
layout (location = 8)  in vec4 vshadow;

#include "lighting.glsl"

#define MAX_BODIES 4
uniform int   body_count = 0;
uniform vec3  body_centre[MAX_BODIES];
uniform vec4  body_inverse[MAX_BODIES];     //world to the body's frame, as a quaternion (xyz, w)
uniform float body_radius[MAX_BODIES];
uniform int   body_style[MAX_BODIES];       //0 a dry rocky planet, 1 a grey moon
uniform vec3  sun_direction = vec3(0.0,1.0,0.0);   //toward the sun
uniform float relief_height = 1.2;          //world units the noise's tallest bump stands
uniform vec3  atmosphere_colour = vec3(0.35,0.62,1.0);
uniform float cloud_cover = 0.5;

vec3 Rotate(vec4 q, vec3 v){
    vec3 t = 2.0 * cross(q.xyz,v);
    return v + q.w * t + cross(q.xyz,t);
}

//Value noise. The hash is the well-known fract one; precise enough at the frequencies used here.
float Hash(vec3 p){
    p = fract(p * 0.3183099 + 0.1);
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float Noise(vec3 x){
    vec3 i = floor(x);
    vec3 f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(Hash(i + vec3(0,0,0)),Hash(i + vec3(1,0,0)),f.x),
                   mix(Hash(i + vec3(0,1,0)),Hash(i + vec3(1,1,0)),f.x),f.y),
               mix(mix(Hash(i + vec3(0,0,1)),Hash(i + vec3(1,0,1)),f.x),
                   mix(Hash(i + vec3(0,1,1)),Hash(i + vec3(1,1,1)),f.x),f.y),f.z);
}

//Octaves fade out once they are finer than a pixel (`w` is the footprint of p across one), so a
//planet seen whole does not shimmer and one seen from the ground still has grain. A faded octave
//gives its mean rather than nothing, so zooming out does not darken the surface.
float Fbm(vec3 p, int octaves, float w){
    float sum = 0.0;
    float norm = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;
    for (int i = 0; i < octaves; i++){
        float fade = clamp(0.6 / max(frequency * w,1e-6) - 1.0,0.0,1.0);
        sum += amplitude * mix(0.5,Noise(p * frequency + vec3(float(i) * 7.31)),fade);
        norm += amplitude;
        frequency *= 2.03;
        amplitude *= 0.5;
    }
    return sum / norm;
}

//A dry world in the Starcom screenshot's colours: rust lowlands, pale sand, dark ridged highlands,
//a little water in the deepest basins.
vec3 PlanetSurface(vec3 p, float w, out float height, out float clouds){
    float continents = Fbm(p * 0.55 + vec3(3.1),5,w * 0.55);
    float detail = Fbm(p * 2.3,9,w * 2.3);
    float ridge = 1.0 - abs(2.0 * Fbm(p * 1.3 + vec3(7.7),7,w * 1.3) - 1.0);
    ridge *= ridge;
    float highland = smoothstep(0.46,0.62,continents);
    height = continents * 0.6 + detail * 0.25 + ridge * 0.4 * highland;

    vec3 rust = vec3(0.42,0.20,0.11);
    vec3 sand = vec3(0.80,0.64,0.44);
    vec3 dark = vec3(0.12,0.10,0.09);
    vec3 water = vec3(0.04,0.09,0.15);
    vec3 c = mix(rust,sand,smoothstep(0.40,0.58,continents + (detail - 0.5) * 0.25));
    c = mix(c,dark,smoothstep(0.35,0.75,ridge * highland));
    float wet = 1.0 - smoothstep(0.255,0.27,height);
    c = mix(c,water,wet);
    c *= 0.82 + 0.36 * detail;

    float cloud_noise = Fbm(p * 0.9 + vec3(11.0,4.0,-6.0),7,w * 0.9);
    clouds = smoothstep(1.0 - cloud_cover * 0.62,1.0 - cloud_cover * 0.62 + 0.18,cloud_noise) * 0.9;
    return c;
}

//A grey moon: dark maria on pale highland, fine grain.
vec3 MoonSurface(vec3 p, float w, out float height){
    float maria = Fbm(p * 0.5 + vec3(5.0),5,w * 0.5);
    float detail = Fbm(p * 2.6,9,w * 2.6);
    height = maria * 0.5 + detail * 0.5;
    vec3 c = mix(vec3(0.46,0.45,0.43),vec3(0.24,0.235,0.23),smoothstep(0.45,0.58,maria));
    return c * (0.8 + 0.4 * detail);
}

void main(){
    //UV derivatives while control flow is still uniform - see SampleMaterialTexture.
    g_uv_dx = dFdx(vuv);
    g_uv_dy = dFdy(vuv);
    SelectMaterial();

    //Which body this fragment lies on: the one whose surface it is nearest.
    int b = 0;
    float best = 1e30;
    for (int i = 0; i < body_count; i++){
        float off = abs(length(vposition - body_centre[i]) - body_radius[i]);
        if (off < best){
            best = off;
            b = i;
        }
    }
    float radius = body_radius[b];
    vec3 dir = normalize(Rotate(body_inverse[b],vposition - body_centre[b]));
    //One noise unit is six ship lengths of surface, whatever the body's size - a 20-radius planet
    //gets a handful of continents across its cap, as the reference screenshot's does.
    vec3 p = dir * radius / 6.0;
    float w = length(fwidth(p));

    float height = 0.0;
    float clouds = 0.0;
    vec3 albedo;
    if (body_style[b] == 0){
        albedo = PlanetSurface(p,w,height,clouds);
    }else{
        albedo = MoonSurface(p,w,height);
    }

    //Relief - see the note at the top. Mikkelsen's surface gradient from the height's screen
    //derivatives, so no second evaluation of the noise.
    vec3 N = normalize(vnormal);
    vec3 L = normalize(sun_direction);
    float bump = height * relief_height;
    vec3 dpdx = dFdx(vposition);
    vec3 dpdy = dFdy(vposition);
    vec3 r1 = cross(dpdy,N);
    vec3 r2 = cross(N,dpdx);
    float det = dot(dpdx,r1);
    vec3 surface_gradient = sign(det) * (dFdx(bump) * r1 + dFdy(bump) * r2);
    vec3 bumped = normalize(abs(det) * N - surface_gradient);
    float smooth_light = dot(N,L);
    float shade = clamp(max(dot(bumped,L),0.0) / max(smooth_light,0.02),0.0,2.5);
    albedo *= mix(1.0,shade,smoothstep(0.0,0.12,smooth_light));

    //Cloud tops are flat and bright, so they go on after the relief.
    albedo = mix(albedo,vec3(0.92,0.93,0.95),clouds);

    vec3 lit = LightSurface(albedo);

    //The air seen edge-on: a rim of the atmosphere's colour on the day side.
    if (body_style[b] == 0){
        vec3 V = normalize(eye_position - vposition);
        float rim = pow(1.0 - clamp(dot(N,V),0.0,1.0),3.0);
        float day = smoothstep(-0.25,0.35,smooth_light);
        lit += atmosphere_colour * rim * day * 0.7;
    }
    color = vec4(lit,1.0);
}
