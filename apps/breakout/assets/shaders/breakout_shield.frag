#version 430 core
//The engine-wide texture unit map - every layout(binding = ...) below names an entry in
//it rather than a number of its own. Mirrored in C++ by core/TextureUnits.h.
#include "texture_units.glsl"


/*
    The shield across the bottom of the paddle game's arena.

    Drawn by Renderer::CustomShaderPass on a single MESH_MODE_SHADER quad, last of the geometry
    passes, blended over everything solid. Everything in here is driven by the simulation - the
    charge is a real gameplay resource that pays for saves and is refilled by breaking bricks, the
    ripples are where the ball actually struck, and the bloom follows the nearest live ball. A
    static pattern would be a texture with extra steps.

    Vertex stage is the stock shaders/default.vert. Every custom shader in the repo reuses it,
    and it used to be dangerous not to: CustomShaderPass calls Shader::Setmat4("mat_worldcam",...)
    on every registered custom shader every frame, and Setmat4 on a missing uniform went through
    debug->Fatal, which calls exit(1). Since GLSL strips a uniform that is declared but never used,
    a custom vertex stage that did not happen to need the camera matrix killed the process on its
    first frame. That edge is gone - every Shader setter now warns once and returns false instead
    of dying - so reusing default.vert is a convenience again rather than a trap.

    --- WHAT THE PASS HANDS US ------------------------------------------------------------------
    The deferred G-buffer, bound on units 1-3 (Renderer::TEXUNIT_GBUFFER_*), which is how a
    translucent surface knows how far away the solid scene behind it is. Used here for the soft
    intersection where the shield passes through the arena's side walls: without it the shield cuts
    a hard diagonal line across them and reads as a decal rather than as a field.

    NOTE ON THE POSITION BUFFER, which cost half an hour: DeferredPass CLEARS it to (1,0,0,0),
    which is a perfectly legal world position about a metre from the origin. So an unwritten texel
    does not read as "nothing here" - it reads as geometry very close to the camera, and the soft
    intersection dissolves the shield against the empty sky. The depth buffer is the one that can
    be asked the question, because its clear value (1.0) is outside the range anything real
    occupies. Sample depth to find out WHETHER there is geometry, then position to find out where.
*/

//--- from shaders/default.vert. The locations matter; copy them exactly. -----------------------
layout (location = 0)  in vec3 vposition;       //fragment position in WORLD space
layout (location = 1)  in vec3 vnormal;
layout (location = 2)  in vec2 vuv;
layout (location = 3)  in mat3 TBN;
layout (location = 6)  flat in int vmatindex;
layout (location = 7)  flat in int vobjid;
layout (location = 8)  in vec4 vshadow;         //meaningless here: CustomShaderPass never sets mat_shadow

layout (location = 0) out vec4 color;

//--- the G-buffer, bound for us by CustomShaderPass -------------------------------------------
//Deliberately NOT declaring default.frag's `material_texture[]` at TEXUNIT_MATERIAL_FIRST: an array of
//samplers claims bindings 0..23 and would collide with these three.
layout (binding = TEXUNIT_GBUFFER_DEPTH) uniform sampler2D gbuffer_depth;
layout (binding = TEXUNIT_GBUFFER_POSITION) uniform sampler2D gbuffer_position;

//--- the scene's real lights, bound globally ---------------------------------------------------
//Copied verbatim from shaders/custom.frag so the layout cannot drift from light_t in core/Light.h.
struct Light{
    vec3    position;
    int     shadow;
    vec3    direction;  //all zero means a point light
    float   brightness;
    vec3    color;
    float   cos_angle;  //0 means a point light, otherwise a cone
    //Size of the source in world units, for the penumbra estimate. Appended last, and the
    //next one must be too - see core/light_t, which this is a hand copy of.
    float   radius;
    float   pad0;
    float   pad1;
    float   pad2;
};
layout (std430, binding = 2) buffer LightBuffer{
    Light lights[];
};

//--- set by CustomShaderPass before our callback ----------------------------------------------
uniform vec3 eye_position;

//--- set by ApplicationBreakout::SetShieldUniforms, from the last tick's state -----------------
//Several of these waste a component, and that is now history rather than a constraint: when this
//was written core/Shader.h offered Setint/Setfloat/Setvec3/Setmat3/Setmat4 and nothing else, so a
//rectangle had to travel as two vec3s and three ripples as three separate uniforms. Setvec2,
//Setvec4 and the v-form array setters exist now, so the natural shape here is a vec2 origin, a
//vec2 size, and one vec4 array of ripples carrying per-impact intensity in the fourth component.
//Left as-is deliberately - this is a working effect, not a demo of the API - but anyone editing
//this file should reshape it rather than adding a ripple3. See docs/breakout_findings.md.
uniform float shield_charge = 1.0;      //0..1, the gameplay resource
uniform float shield_flare  = 0.0;      //0..1, decaying after a save
uniform float shield_time   = 0.0;      //SIMULATION TICKS, so the effect pauses with the game
uniform vec3  shield_origin = vec3(0.0);//(min_x, min_y, unused) of the quad in world space
uniform vec3  shield_size   = vec3(1.0);//(width, height, unused)
uniform vec3  shield_focus  = vec3(0.0);//world position of the nearest live ball
//(world_x, age_in_ticks, unused). A negative age means the slot is idle.
uniform vec3  ripple0 = vec3(0.0,-1.0,0.0);
uniform vec3  ripple1 = vec3(0.0,-1.0,0.0);
uniform vec3  ripple2 = vec3(0.0,-1.0,0.0);

#define PI 3.14159265359

//How fast a ripple ring spreads, in world units per simulation tick, and how long it lives.
#define RIPPLE_SPEED    0.13
#define RIPPLE_LIFE     45.0

//One expanding ring. Returns 0 for an idle slot, so the caller needs no branch.
float Ripple(vec3 rip, vec2 p, float surface_y){
    if (rip.y < 0.0){
        return 0.0;
    }
    float age    = rip.y;
    float radius = age * RIPPLE_SPEED;
    float d      = distance(p,vec2(rip.x,surface_y));
    //A gaussian shell rather than a smoothstep band: the ring stays the same thickness as it
    //grows, which is what makes it read as a wave front instead of as a widening donut.
    float shell  = exp(-pow((d - radius) * 2.4,2.0));
    float fade   = clamp(1.0 - age / RIPPLE_LIFE,0.0,1.0);
    //Squared so the tail of a ripple disappears quickly and the head is what is seen.
    return shell * fade * fade;
}

void main(){
    vec2 p = vposition.xy;

    //Position within the quad, 0..1. shield_size carries the quad's real extents rather than the
    //shader assuming them, so moving or resizing the shield is a change in one place.
    vec2 local = (p - shield_origin.xy) / max(shield_size.xy,vec2(0.0001));

    //The collision surface itself, drawn as a bright line. This is not decoration: it is exactly
    //the line breakout/Field.cpp reflects a descending ball off, so the player can see where the
    //save will happen rather than having to learn it.
    float surface_y = shield_origin.y + shield_size.y * 0.5;
    float surface   = 1.0 - smoothstep(0.0,0.055,abs(local.y - 0.5));

    //--- the lattice ---------------------------------------------------------------------------
    //Two sets of diagonals drifting in opposite directions. Denominated in shield_time, which is
    //a TICK count published by the simulation - so the pattern freezes when the game is paused and
    //advances exactly one step per sim_step, which is what makes a single frame of it reviewable.
    float l1 = abs(fract((p.x + p.y) * 0.62 + shield_time * 0.0045) - 0.5);
    float l2 = abs(fract((p.x - p.y) * 0.62 - shield_time * 0.0045) - 0.5);
    float lattice = (1.0 - smoothstep(0.0,0.085,l1)) + (1.0 - smoothstep(0.0,0.085,l2));
    lattice *= 0.5;

    //--- impacts -------------------------------------------------------------------------------
    float rings = Ripple(ripple0,p,surface_y)
                + Ripple(ripple1,p,surface_y)
                + Ripple(ripple2,p,surface_y);

    //--- the ball coming down ------------------------------------------------------------------
    //The shield tightens under whichever ball is closest to it, so the player's eye is pulled to
    //where the save is about to be needed. Falls off fast, or the whole pane just glows.
    float focus = exp(-distance(p,shield_focus.xy) * 0.20);

    //--- charge ---------------------------------------------------------------------------------
    //An exhausted shield is not invisible, it is a GHOST: the lattice is still faintly there so
    //the player can see the thing that is no longer going to save them. That reads far better
    //than the pane simply vanishing, which looks like a rendering bug.
    float live      = smoothstep(0.0,0.34,shield_charge);   //0.34 is the cost of one save
    float intensity = mix(0.10,1.0,live) * mix(0.55,1.0,shield_charge);

    /*
        The flare is SQUARED and deliberately modest.

        It started at a flat 1.6 and whited out the entire pane for most of a second - which drowned
        the impact ring, the lattice and the charge colour all at once, so the one frame that had
        the most to say said nothing. Squaring it makes it a punch rather than a wash (it is gone in
        a third of the time) and dropping the coefficient leaves headroom for the ring, which is the
        part that actually tells the player WHERE the ball struck.
    */
    float flare = shield_flare * shield_flare;

    float energy = intensity * (0.10
                              + 0.42 * lattice
                              + 1.30 * surface
                              + 0.55 * focus)
                 + 3.20 * rings
                 + 0.65 * flare;

    //--- colour ---------------------------------------------------------------------------------
    //Red when empty, amber mid, cyan when full - the same reading as a fuel gauge, and legible at
    //a glance while the ball is the thing actually being watched.
    vec3 empty_hue = vec3(1.00,0.16,0.12);
    vec3 half_hue  = vec3(1.00,0.66,0.14);
    vec3 full_hue  = vec3(0.26,0.92,1.00);
    vec3 hue = (shield_charge < 0.5)
             ? mix(empty_hue,half_hue,shield_charge * 2.0)
             : mix(half_hue,full_hue,(shield_charge - 0.5) * 2.0);

    //A ring and a flare are always white-hot at their core, whatever the charge is - an impact
    //should read as an impact rather than as more of the same colour.
    vec3 rgb = mix(hue,vec3(1.0),clamp(rings + flare,0.0,1.0) * 0.75);

    //--- the scene's own lights ------------------------------------------------------------------
    //The ball carries a point light, so this is what makes the shield catch it as the ball comes
    //down. Reading the global light SSBO is one of the genuinely nice things about this pass: the
    //effect is lit by the same lights as everything else without the app wiring anything up.
    vec3 lit = vec3(0.0);
    for (int i = 0; i < lights.length(); i++){
        //A point light is the one with no direction; a sun would wash the whole pane evenly and
        //there is nothing to learn from that.
        if (dot(lights[i].direction,lights[i].direction) > 0.0001){
            continue;
        }
        vec3 to_light = lights[i].position - vposition;
        float d2 = dot(to_light,to_light);
        lit += lights[i].color * lights[i].brightness / (1.0 + d2 * 0.5);
    }
    rgb += lit * 0.18 * intensity;

    //--- soft intersection against the solid scene -------------------------------------------------
    //Where the shield passes through the arena walls, fade it out instead of cutting a line.
    //Depth first, to find out whether there IS anything behind this fragment - see the note at the
    //top about the position buffer's clear value.
    vec2 screen_uv = gl_FragCoord.xy / vec2(textureSize(gbuffer_position,0));
    float scene_depth = texture(gbuffer_depth,screen_uv).r;
    float intersect_fade = 1.0;
    if (scene_depth < 0.99999){
        vec3 scene_world = texture(gbuffer_position,screen_uv).xyz;
        float behind = distance(scene_world,eye_position) - distance(vposition,eye_position);
        //1.1 world units of softness: wide enough to hide the wall's edge, narrow enough that the
        //middle of the pane - four units clear of the back panel - is untouched.
        intersect_fade = clamp(behind / 1.1,0.0,1.0);
    }

    //--- the quad's own edges ----------------------------------------------------------------------
    float edge = smoothstep(0.0,0.05,local.x) * (1.0 - smoothstep(0.95,1.0,local.x))
               * smoothstep(0.0,0.14,local.y) * (1.0 - smoothstep(0.86,1.0,local.y));

    float alpha = clamp(energy,0.0,1.0) * intersect_fade * edge;
    //Blending is GL_SRC_ALPHA / GL_ONE_MINUS_SRC_ALPHA (Renderer::SetOpenGLState), so premultiply
    //nothing - but do let the colour run past 1 where the energy does, which is what gives an
    //impact ring its bloom against the dark back panel.
    color = vec4(rgb * energy,alpha);
}
