#version 460 core
/*
    A firefly's glow: a small hot core and a wide soft halo, both warm yellow-green. The engine has
    no bloom, so the halo IS the bloom - drawn, not computed - and it is what grows when the fly
    flashes, while the core only brightens. At the ember between flashes both are faint, so a fly
    is a dim speck until it lights.
*/
layout (location = 0) in vec2 vuv;
layout (location = 1) in float vbright;

layout (location = 0) out vec4 color;

uniform vec3 core_color = vec3(1.0,0.97,0.70);
uniform vec3 halo_color = vec3(0.72,1.0,0.30);
uniform float halo_strength = 0.75;

void main(){
    float r2 = dot(vuv,vuv);
    if (r2 >= 1.0){
        discard;
    }
    float core = (1.0 - smoothstep(0.0,0.06,r2)) * (0.35 + 0.65 * vbright);
    //Wide and soft: at exp(-5 r^2) a flash read as a dot, not a glow.
    float halo = exp(-r2 * 3.2) * (1.0 - r2) * vbright * halo_strength;
    float a = clamp(core + halo,0.0,1.0);
    vec3 c = mix(halo_color,core_color,core / max(core + halo,1e-4));
    color = vec4(c,a);
}
