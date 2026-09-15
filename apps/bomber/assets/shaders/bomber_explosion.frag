#version 430 core

/*
    A raymarched explosion, in the two shapes a bomberman blast can be built from.

    Same vehicle as the ship's clouds (shaders/raymarch_volume.frag, which this is descended
    from): an ordinary instanced box mesh tagged MESH_MODE_SHADER, so only the pixels the box
    covers ever run the march, and the box is positioned and scaled by moving the Object.
    default.vert hands us vmatselect (= gl_InstanceID) so we read this instance's own transform
    out of the instance SSBO and march the unit cube in LOCAL space - which means several blasts
    still batch into one draw call.

    --- THE TWO MODES, AND WHY BOTH EXIST -------------------------------------------------------
    A bomberman blast is a plus/cross of tiles. There are two honest ways to draw it and this file
    does both, because which one is right is a judgement about how they LOOK next to each other:

      BLAST_MODE_TILE  - one volume per tile of the cross, the way the game itself is built. Each
                         instance is a ball a tile wide, and the cross is the union of them. Each
                         tile lights up a few ticks after the one nearer the bomb, so the flame
                         visibly runs outward - which is what the original does. Reaches exactly
                         the tiles the maze allows, because it IS the tiles.

      BLAST_MODE_CROSS - ONE volume for the whole blast, whose density is a distance field around
                         a cross-shaped skeleton with per-arm lengths. One march, so it composites
                         correctly against itself, and the arms grow smoothly rather than in steps.

    The trade-off the bench exists to show: the tile version is N instances that do NOT depth-sort
    against each other (a volume writes no depth, and CustomShaderPass draws instances in whatever
    order the renderer collected them), so where two tile balls overlap they blend in instance
    order and the seam can be seen. The cross version cannot have that problem because there is
    nothing to blend against - but it marches one big box, most of which is empty, and its arm
    lengths have to be fed in as uniforms rather than falling out of the tile list.

    THE SHAPE CODE IS SHARED, not duplicated: a ball is the degenerate cross whose four arms have
    zero length, so `skeleton_distance` handles both and every term downstream - shell, rim, noise,
    temperature - is written once. That is deliberate. Two copies would drift, and then the
    comparison would be between two shaders rather than between two ways of arranging one.

    --- WHAT MAKES IT AN EXPLOSION RATHER THAN A CLOUD ------------------------------------------
      - IT IS EMISSIVE. A cloud is lit; a fireball makes its own light, and almost all of what
        reaches the eye is its own emission. So the march accumulates a blackbody-ish colour from
        a TEMPERATURE field rather than integrating incoming radiance, and the scene's lights only
        matter once the fire has cooled into smoke.

      - IT HAS AN AGE. Every shape term below is a function of `blast_age`, a count of SIMULATION
        TICKS since detonation published by ApplicationBomber - so the effect freezes under
        sim_pause and advances exactly one step per sim_step, which is what makes a single frame of
        a blast reviewable and a screenshot of frame N reproducible. A wall clock here would make
        every one of those impossible.

      - THE SHAPE IS A SHELL, NOT A VOLUME OF FOG. A fireball is burning gas at its expanding front
        with exhausted gas behind it, so density peaks at a radius that grows and the middle
        hollows out as it goes. Marching a solid ball reads as an orange balloon; marching the
        shell reads as a detonation. (A flame ARM is the other way round - see `core_heat`.)

    --- UNITS ------------------------------------------------------------------------------------
    Everything here is in the volume's OBJECT space, where the box is the unit cube spanning
    -0.5..+0.5, and THE APP CONVERTS: ApplicationBomber::PushBlastUniforms divides each world-unit
    setting by that volume's box size before pushing it. So the knobs are in metres and tiles,
    which is what they are worth talking about in, and the march stays in the space `ray_box_dst`
    needs. The conversion assumes the box is UNIFORMLY scaled and unrotated, which both of these
    volumes are - a non-uniform box would make an object-space length stop being a world length,
    which is the trap raymarch_volume.frag documents at its light march.

    --- THE DEBUG VIEWS ARE WORTH KNOWING ABOUT --------------------------------------------------
    `f_show_box` (the Explosion panel's Debug view, or the bomber_set MCP tool) answers the two
    questions that otherwise cost a screenshot each: 1 shows the marched interval, so a blank
    screen can be told apart from a box that is not being intersected, and 2 shows the G-buffer
    this shader is handed, so "the depth clamp is wrong" can be told apart from "the depth clamp is
    not reaching me".
*/

layout (location = 0) out vec4 color;

//From default.vert.
layout (location = 0) in vec3 vposition;        //Fragment position in world space
layout (location = 9) flat in int vmatselect;   //= gl_InstanceID, our index into instance_data

//All the light types fall together into a single light struct - matches light_t in Light.h.
//Copied verbatim rather than included so the layout cannot drift from core/light_t.
struct Light{
    vec3    position;
    int     shadow;     // Set if the light produces a shadow
    vec3    direction;  // Direction of 0 means its a point light
    float   brightness;
    vec3    color;
    float   cos_angle;  // 0 means its a point light, else it becomes a cone light
    float   radius;
    float   pad0;
    float   pad1;
    float   pad2;
};

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

layout (std430, binding = 2) buffer LightBuffer{
    Light lights[];
};

//The deferred G-buffer of the solid scene, bound by Renderer::CustomShaderPass. The binding
//numbers are TEXUNIT_GBUFFER_* in Renderer.h and have to stay in step with them.
layout (binding = 1) uniform sampler2D gbuffer_depth;      //Window-space depth, 1.0 where nothing was drawn
layout (binding = 2) uniform sampler2D gbuffer_position;   //World-space position of the solid scene

//The tileable 3D worley from shaders/noise3d.comp, bound by the app at TEXUNIT_APP_RESERVED
//(Renderer.h). All four channels are the same noise at doubling frequencies, so the FBM below
//costs ONE fetch. Shared with the ship app - see shared_assets/shaders/noise3d.comp.
layout (binding = 25) uniform sampler3D noise_texture;

//The ray origin. The default shaders get it under this same name.
uniform vec3 eye_position;

//--- which shape this program draws -------------------------------------------------------------
#define BLAST_MODE_TILE     0
#define BLAST_MODE_CROSS    1
//Set once per program, not per frame: the app builds the SAME source twice into two Shader
//objects, one per mode, because uniform_callback is per-Shader and both modes are on screen at
//once. See ApplicationBomber::BuildExplosion.
uniform int blast_mode = BLAST_MODE_TILE;

//--- the blast clock ----------------------------------------------------------------------------
//Both in SIMULATION TICKS. A NEGATIVE age means no blast is live, which is how the app says "draw
//nothing" without having to hide the object - see ApplicationBomber::SetExplosionUniforms.
uniform float blast_age  = -1.0;
uniform float blast_life = 110.0;
//Shifts the noise so two detonations in the same place do not produce the same billows. The app
//advances it per detonation rather than per frame: a seed that moved every frame would make the
//smoke boil instead of drift.
uniform float blast_seed = 0.0;

//--- the cross, in tiles ------------------------------------------------------------------------
//How far the flame is ALLOWED to reach in each direction, IN TILES: (east +X, west -X, north -Z,
//south +Z). The app walks the maze from the bomb and stops at the first wall, so a blocked arm is
//short here and not clipped later. Both modes read it - TILE to decide whether this instance is
//one of the tiles that lights up at all, CROSS to size its skeleton - and it is in tiles for both
//rather than tiles for one and lengths for the other, which is the kind of double meaning that
//survives exactly as long as the person who wrote it remembers.
uniform vec4 arm_limit = vec4(0.0);
//One grid cell, in this volume's object units. What CROSS mode multiplies arm_limit by to get a
//skeleton length it can measure against.
uniform float cell_object = 0.1667;
//World-space centre of the blast, and one grid cell in world units. TILE mode only, and both in
//WORLD space deliberately: they are compared against this instance's own world translation, which
//is what says which tile it is and therefore how long it waits.
uniform vec3 blast_origin = vec3(0.0);
uniform float cell_world = 2.0;
//Ticks each ring of tiles waits behind the one nearer the bomb. TILE mode only - it is what makes
//the flame run outward in steps, the way the original does. CROSS mode grows its arms smoothly
//instead, which is the other half of what these two modes are here to be compared on.
uniform float tile_delay = 4.0;

//--- shape --------------------------------------------------------------------------------------
/*
    EVERY ONE OF THESE EXCEPT rise IS A FRACTION OF THE FIREBALL, NOT OF THE BOX, and that is not a
    detail: the fireball is a tenth of the box early on and most of it at the end, so a shell
    thickness, a rim width or a billow size fixed in box units means a completely different-looking
    fire at the two ends of one blast. Measured against the front's own radius they stay what they
    were set to. The first version of this file did it the other way round and the first tenth of a
    second rendered as a smooth white marble - there was less than one noise cell across the whole
    ball to roughen it.
*/
uniform float blast_radius   = 0.30;  // Radius the flame front reaches, in OBJECT units
uniform float shell_thickness= 0.45;  // Depth of the burning front, as a fraction of the radius
uniform float rim_softness   = 0.22;  // Width of the fade at the front's outer edge, ditto
uniform float turbulence     = 0.85;  // How far the noise is allowed to push the front in and out
uniform float noise_scale    = 1.20;  // How many times the noise tiles across the flame's DIAMETER
uniform float outflow        = 0.35;  // Noise-space distance the billows are dragged outward over a life
uniform float rise           = 0.20;  // How far the flame floats up over a life, in OBJECT units
uniform float blast_density  = 3.0;   // Density per world unit at the heart of the front

//--- fire and smoke -----------------------------------------------------------------------------
uniform float heat              = 0.90; // Scales the temperature field before the colour ramp
uniform float emission_strength = 0.55; // Radiance the hottest gas emits
uniform float smoke_albedo      = 0.55; // How much of the scene's light cold smoke bounces back
uniform vec3  smoke_ambient     = vec3(0.05,0.05,0.06);
uniform float light_absorption  = 1.0;  // Multiplier on how fast light is extinguished
/*
    Where the gas is hottest: 0 at the expanding FRONT, 1 along the skeleton at the CORE.

    The two shapes genuinely want opposite answers and it is not a matter of taste. A fireball
    burns at its front and drags exhausted gas behind it, so the front is the hot part and the
    middle is already smoke. A flame running down a corridor is a jet: the hot part is its axis and
    what is at the edge is the part that has met cold air. Setting this per mode is what lets one
    temperature field serve both.
*/
uniform float core_heat         = 0.0;
/*
    How much cloud brightness one unit of the sun's own `brightness` is worth.

    A scale is needed because a surface turns radiance into pixels through a BRDF and an NdotL
    while the volume integrates it over density and step length, so the same number does not land
    in the same place - the same reason ApplicationShip's clouds carry this uniform. It is a SCALE
    ON the sun's brightness though, not a replacement: dragging the sun in the light inspector
    still changes the smoke.

    Leaving it out was worth two screenshots. At the app's sun brightness of 3.2 the scattered sun
    came out five times brighter than the fire itself, so the whole blast - fireball included -
    rendered as a white cloud with a pink edge, and it looked like a bug in the colour ramp rather
    than like the sun being in the wrong units.
*/
uniform float sun_intensity     = 0.12;

//--- march --------------------------------------------------------------------------------------
uniform int num_view_steps  = 40;   // Steps along the view ray
uniform int num_light_steps = 4;    // Steps towards each light per view step, for the smoke only
uniform float light_falloff = 2.0;  // brightness/pow(distance,this)
uniform float max_radiance  = 10.0; // Ceiling on in-scattered radiance at one sample

//Lights past this are ignored - a bound on the worst case, since the march costs
//num_view_steps * lights * num_light_steps.
#define MAX_BLAST_LIGHTS 8

//1 = read out the marched interval (is the box being intersected as a slab?).
//2 = read out the G-buffer this shader is given: red where depth says there is geometry, blue
//where it says there is not. All blue means the G-buffer is not reaching us.
uniform int f_show_box = 0;

//The volume fills the unit cube in object space, matching ApplicationBomber::BuildBlastCube.
#define BOX_MIN vec3(-0.5)
#define BOX_MAX vec3( 0.5)

/*
    --- PER-INSTANCE STATE, set once in main() before the march ----------------------------------
    File-scope rather than passed down because `medium_at` is also called from inside
    `light_march`, and threading two more parameters through both for the sake of purity would
    only make the call sites harder to read. Nothing writes these after main() has set them.
*/
float g_t = 0.0;            //this instance's normalised age, 0 at its detonation and 1 at its end
vec4  g_reach = vec4(0.0);  //how far each arm has got RIGHT NOW, object units, E/W/N/S

/*
    Slab test against the unit cube. Returns (distance to the box, distance travelled inside it),
    both as values of the ray parameter t.

    Called with a ray already taken into object space but NOT renormalised: the transform is
    affine, so inv*(ro + t*rd) == (inv*ro) + t*(inv*rd) and the same t describes both rays. rd is
    a unit vector in WORLD space, so every t here - and every step size built from it - is a real
    world distance, which is what the density and Beer's law need. Renormalising the local
    direction would silently break that on any non-uniformly scaled volume.
*/
vec2 ray_box_dst(vec3 local_origin, vec3 inv_local_dir){
    vec3 t0 = (BOX_MIN - local_origin) * inv_local_dir;
    vec3 t1 = (BOX_MAX - local_origin) * inv_local_dir;
    vec3 tsmall = min(t0,t1);
    vec3 tbig   = max(t0,t1);

    float dst_a = max(max(tsmall.x,tsmall.y),tsmall.z);   //Entry
    float dst_b = min(min(tbig.x,tbig.y),tbig.z);         //Exit

    float dst_to_box = max(0.0,dst_a);                    //0 when the eye is inside the box
    float dst_inside = max(0.0,dst_b - dst_to_box);       //0 when the box is missed or behind us
    return vec2(dst_to_box,dst_inside);
}

/*
    How far the flame front has travelled, as a fraction of wherever it is going.

    (1-(1-t)^5) rather than a straight ramp: a detonation is nearly all over in its first fifth,
    and the eye reads the DECELERATION as the blast having had force behind it. A linear expansion
    reads as a balloon being inflated, which was the first thing this looked like. The exponent was
    3 to begin with and that was still too gentle - the ball was at a quarter of its size a tenth
    of the way through its life, so the flash and the expansion read as two separate events. At 5
    it is at 40% by then and past 75% by a quarter.
*/
float expansion(float t){
    return 1.0 - pow(1.0 - t,5.0);
}

//The tube radius of the flame now. The 0.02 floor keeps the radius-relative terms below from
//dividing by zero on the first tick, and is small enough to be invisible.
float front_radius(float t){
    return max(blast_radius * expansion(t),0.02);
}

/*
    Distance from q to the blast's SKELETON: the set of points the flame is centred on.

    For a cross that is four segments running out from the bomb along ±X and ±Z, each as long as
    its arm has currently got. For a single ball every arm has zero length and all four segments
    collapse to the origin, so this returns length(q) and the ball falls out of the cross code
    rather than needing any of its own. That is the whole reason the two modes share a file.

    Each term is the standard point-to-segment distance: clamp the coordinate along the segment's
    axis into the segment's span, and measure to that.
*/
float skeleton_distance(vec3 q){
    //The ±X arms, as one segment spanning -west .. +east.
    float dx = length(vec3(q.x - clamp(q.x,-g_reach.y,g_reach.x),q.y,q.z));
    //The ±Z arms. -Z is north, +Z is south, matching ApplicationBomber's grid.
    float dz = length(vec3(q.x,q.y,q.z - clamp(q.z,-g_reach.z,g_reach.w)));
    return min(dx,dz);
}

/*
    The medium at p (object space), as (density, temperature).

    Both come out of one evaluation because they share every expensive term - the noise fetch, the
    distance to the skeleton - and because they have to AGREE: gas that is dense at the front must
    also be the gas that is hot there, or the fire and the smoke separate visibly.
*/
vec2 medium_at(vec3 p){
    float t = g_t;

    //The flame floats up as it burns, so it is not bolted to the floor. Quadratic, so the rise is
    //something the fire does once it has stopped expanding rather than a drift from detonation.
    vec3 q = p - vec3(0.0,rise * t * t,0.0);

    float r = front_radius(t);
    float d = skeleton_distance(q);

    //Outward from the bomb, for dragging the noise along. Measured from the CENTRE rather than
    //from the nearest point of the skeleton: along an arm that still points the way the gas is
    //travelling, which is what this is for, and it costs nothing.
    float qlen = length(q);
    vec3 dir = q / max(qlen,0.0001);

    /*
        The noise, sampled in units of the FLAME and dragged outward as the blast ages.

        Dividing by the diameter is what keeps the same number of billows across the flame while it
        grows, which is both what a real fireball does - its structures expand with it - and the
        only way the early frames get any texture at all. Sampling in box units instead gave a ball
        a fifth of a noise cell across, i.e. a constant, i.e. a smooth sphere.

        The outward drag is what stops the billows sitting still while the front passes through
        them, which reads as the fireball being a window onto a texture rather than as gas.
        `blast_seed` shifts the whole field so the next bomb in the same place is not the same bomb.
    */
    vec3 uvw = (q / (r * 2.0)) * noise_scale - dir * (outflow * t) + vec3(blast_seed);
    vec4 n = texture(noise_texture,uvw);
    float fbm = n.r * 0.55 + n.g * 0.25 + n.b * 0.13 + n.a * 0.07;

    //The front, pushed in and out by the noise so the flame is lumpy rather than a smooth tube.
    float lumpy = r * (1.0 + (fbm - 0.5) * turbulence);
    //Every soft edge below is this wide. A fraction of the radius, so the flame is not a hard
    //marble while it is small and a soft blob once it is big.
    float rim = max(rim_softness * r,0.004);

    //Outer edge: inside the front, fading over `rim`.
    float outer = 1.0 - smoothstep(lumpy - rim,lumpy + rim,d);
    //Inner edge: the exhausted core, a fixed fraction of the front's radius - so the shell grows
    //with the flame instead of becoming a thin skin on a big one. shell_thickness 1 means no core
    //at all, which is what an arm wants: a hollow tube would read as a pipe.
    float inner_r = lumpy * (1.0 - clamp(shell_thickness,0.0,1.0));
    float inner = smoothstep(inner_r - rim,inner_r + rim,d);
    //Only hollow once there IS a core to hollow. At detonation the whole thing is burning, and
    //that solid first instant is what reads as the flash.
    float shell = outer * mix(1.0,inner,smoothstep(0.0,0.30,t));

    //Fade the last quarter of the life out rather than letting the smoke vanish on a frame.
    float fade = 1.0 - smoothstep(0.72,1.0,t);
    float density = shell * fade * max(blast_density,0.0);

    /*
        Temperature: 1 in the hottest gas at detonation, falling with age and with distance from
        wherever `core_heat` says the heat is.

        The `1 - t` term is the gas cooling. The noise is folded in so the fire is mottled rather
        than a smooth gradient, which is most of what stops it looking like a gradient.
    */
    float cooling = pow(1.0 - t,1.8);
    float at_front = 1.0 - smoothstep(0.0,max(lumpy - inner_r,0.0001),max(lumpy - d,0.0));
    /*
        The core profile is NOT just (1 - at_front), and the difference is visible rather than
        pedantic. A linear ramp from the axis leaves more than half the tube below half
        temperature, so most of what the eye sees of an arm is the cool part and the whole blast
        reads as smoke that happens to be cross-shaped. A jet is hot through most of its section
        with a thin cool skin where it has met the air, which is what the smoothstep gives: fully
        hot for the inner 55% of the radius, falling off over the rest.
    */
    float at_core = smoothstep(0.0,0.45,1.0 - at_front);
    float hot = mix(at_front,at_core,clamp(core_heat,0.0,1.0));
    float temperature = clamp(cooling * mix(0.35,1.0,hot) * (0.55 + fbm * 0.9) * heat,0.0,1.0);

    return vec2(density,temperature);
}

/*
    Emitted radiance for a temperature in 0..1.

    A hand-drawn ramp rather than a real blackbody curve: the useful range of an explosion is a
    couple of thousand kelvin wide and the eye reads the SEQUENCE (soot, deep red, orange, yellow,
    white) rather than the spectrum. Cubed at the top so the hottest gas is disproportionately
    bright, which is what makes the core blow out to white against an orange flame.
*/
vec3 fire_color(float temp){
    vec3 c = mix(vec3(0.18,0.03,0.01),vec3(1.00,0.22,0.04),smoothstep(0.00,0.35,temp));
    c = mix(c,vec3(1.00,0.62,0.13),smoothstep(0.30,0.65,temp));
    c = mix(c,vec3(1.00,0.93,0.72),smoothstep(0.62,0.88,temp));
    c = mix(c,vec3(1.00,1.00,0.98),smoothstep(0.86,1.00,temp));
    return c * (0.25 + temp * temp * temp * 3.0);
}

/*
    Transmittance from p towards a light: how much of that light survives the smoke between the
    two. This is what makes one part of the cloud shadow another.

    `local_dir` must be an object-space direction whose world image is a unit vector, so the step
    sizes here - and `max_dst` - are world distances. See the note at the call site.
*/
float light_march(vec3 p, vec3 local_dir, float max_dst, int steps){
    float dst_inside = min(ray_box_dst(p,1.0 / local_dir).y,max_dst);
    if (steps < 1 || dst_inside <= 0.0){
        return 1.0;
    }
    float step_size = dst_inside / float(steps);
    float total = 0.0;
    for (int i = 0;i < steps;i++){
        p += local_dir * step_size;
        total += medium_at(p).x * step_size;
    }
    return exp(-total * light_absorption);
}

/*
    Which kind of light lights[i] is. The engine packs all three into one light_t
    (Renderer::UploadLights): no direction means a point light, a direction with no cos_angle is
    the sun, and a direction WITH a cos_angle is a cone. The 0.1 threshold matches default.frag.
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

/*
    Works out what THIS instance is drawing, and returns false if it is drawing nothing.

    In CROSS mode there is one instance and it is the whole blast, so this is just the clock.

    In TILE mode there is one instance per tile of the cross, and the instance has to work out
    which tile it is. It does that from its OWN WORLD POSITION - the translation column of its
    instance transform, against the bomb's position and the cell size - rather than from any
    per-instance payload. That is deliberate: gl_InstanceID would be the obvious index, but it
    enumerates instances in whatever order the renderer collected them for the draw, which is not
    something this shader gets to assume. A tile's position is a fact about the tile.

    From that position come both of the things a tile needs: which arm it is on and how far out
    (so an arm the maze blocks simply has no lit tiles past the wall), and how long it waits before
    it ignites, which is what makes the flame run outward instead of appearing all at once.
*/
bool setup_instance(mat4 transform){
    if (blast_mode == BLAST_MODE_CROSS){
        if (blast_age < 0.0 || blast_age > blast_life){
            return false;
        }
        g_t = clamp(blast_age / max(blast_life,1.0),0.0,1.0);
        //The arms travel at ONE speed and STOP where the maze stops them, rather than each growing
        //to its own limit over the same time - so a blocked arm finishes early and a free one
        //keeps going, which is what a blast running down a corridor does. Getting this wrong is
        //not subtle: arms scaled to their own limits all finish together, and the blast reads as a
        //cross being inflated rather than as flame travelling.
        float reached = expansion(g_t) * max(max(arm_limit.x,arm_limit.y),
                                             max(arm_limit.z,arm_limit.w));
        g_reach = min(arm_limit,vec4(reached)) * cell_object;
        return true;
    }

    //--- TILE mode ---------------------------------------------------------------------------
    //A ball, so the skeleton stays a point: every arm length is zero.
    g_reach = vec4(0.0);

    vec3 offset = transform[3].xyz - blast_origin;
    float cells_x = offset.x / max(cell_world,0.0001);
    float cells_z = offset.z / max(cell_world,0.0001);
    //Which ring out from the bomb. Rounded, because a tile's centre is on its cell and the
    //arithmetic only has to survive float error, not arbitrary placement.
    float ring = max(abs(cells_x),abs(cells_z));
    ring = floor(ring + 0.5);

    //Which arm, and therefore which limit applies. The centre tile (ring 0) is always lit, and
    //falls out of this without a special case: every limit is at least 0.
    float limit = arm_limit.x;                                  //east, +X
    if (abs(cells_x) > abs(cells_z)){
        limit = (cells_x > 0.0) ? arm_limit.x : arm_limit.y;    //east / west
    }else if (abs(cells_z) > abs(cells_x)){
        limit = (cells_z < 0.0) ? arm_limit.z : arm_limit.w;    //north (-Z) / south (+Z)
    }
    if (ring > limit + 0.5){
        return false;   //the maze stops the flame before this tile
    }

    //Each ring waits for the one inside it. A tile that has not caught yet, or has burnt out,
    //draws nothing - so the tiles of one blast are at different points of the same curve, which
    //is exactly the staggered flame the original game has.
    float local_age = blast_age - ring * tile_delay;
    if (local_age < 0.0 || local_age > blast_life){
        return false;
    }
    g_t = clamp(local_age / max(blast_life,1.0),0.0,1.0);
    return true;
}

void main(){
    mat4 transform = instance_data[vmatselect].mat_transformscale;
    //Nothing is burning here. Cheapest possible exit, taken on every pixel of every box for most
    //of the app's life - the objects stay in the scene rather than being hidden, so that a
    //detonation is one uniform changing and never a visibility flag racing the render thread.
    if (!setup_instance(transform)){
        discard;
    }

    mat4 to_local = inverse(transform);

    //The ray. We are rasterising the box's back faces, so vposition is the far side of the box,
    //but the analytic intersection below does not care which face got rasterised - it works
    //identically with the camera outside or inside the volume.
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

        The depth test on the box's own faces only settles whether this pixel is drawn at all; it
        says nothing about a wall standing PART WAY through the volume, which without this would be
        buried under the full depth of fire instead of the right fraction of it. Depth is read first
        purely as the "is there anything here at all" test - see the TEXUNIT_GBUFFER_* block in
        core/Renderer.h for why the position buffer cannot answer that.
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
        float d = clamp(dst_inside * 0.2,0.0,1.0);
        color = vec4(vec3(1.0),d);
        return;
    }
    if (f_show_box == 2){
        float gdepth = texture(gbuffer_depth,screen_uv).r;
        vec3 gpos = texture(gbuffer_position,screen_uv).xyz;
        float has_geometry = gdepth < 1.0 ? 1.0 : 0.0;
        color = vec4(has_geometry,
                     clamp(length(gpos - eye_position) / 100.0,0.0,1.0),
                     1.0 - has_geometry,
                     1.0);
        return;
    }

    //The sun's direction is constant over the whole volume, so it is taken to object space once,
    //out here. `direction` is the way the light TRAVELS, so towards it is the negative.
    vec3 sun_dir = vec3(0,1,0);
    vec3 sun_color = vec3(1,1,1);
    float sun_brightness = 1.0;

    int num_lights = min(lights.length(),MAX_BLAST_LIGHTS);
    //Point and cone light positions in object space, so the per-sample work below is a subtract
    //rather than a matrix multiply. Entries for the sun are written but never read.
    vec3 light_local[MAX_BLAST_LIGHTS];
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
    //world-space one. Needed because every distance that matters - the light march's step length
    //and the falloff - has to be measured in world units.
    mat3 to_world_rot = mat3(transform);

    float step_size = dst_inside / float(max(num_view_steps,1));
    //Half a step in, so samples sit in the middle of the slabs they represent.
    vec3 p = local_origin + local_dir * (dst_to_box + step_size * 0.5);

    float transmittance = 1.0;
    vec3 radiance = vec3(0.0);
    for (int i = 0;i < num_view_steps;i++){
        vec2 medium = medium_at(p);
        float density = medium.x;
        float temperature = medium.y;
        if (density > 0.0){
            //--- what this sample emits --------------------------------------------------------
            //The dominant term by a wide margin, and the reason this shader is cheap where the
            //cloud is not: emission needs no march towards anything.
            vec3 emitted = fire_color(temperature) * emission_strength;

            /*
                --- what this sample SCATTERS ----------------------------------------------------
                Only the cold gas is worth lighting: smoke is what the scene's lights are visible
                on, and fire outshines them by orders of magnitude. Weighting by (1-temperature)
                also means the expensive half of the loop switches itself off for exactly the
                samples that are bright enough not to need it.
            */
            float smokiness = 1.0 - temperature;
            vec3 scattered = smoke_ambient;
            if (smokiness > 0.02 && num_light_steps > 0){
                scattered += sun_color * sun_brightness * sun_intensity
                           * light_march(p,local_sun_dir,1.0e9,num_light_steps);

                for (int li = 0;li < num_lights;li++){
                    if (light_is_sun(li)){
                        continue;
                    }
                    /*
                        The direction to a local light changes at every sample, and normalising it
                        in object space would be WRONG: under a non-uniform scale the normalised
                        local vector is not the local image of the normalised world one, and this
                        whole shader depends on t being a world distance. Dividing the object-space
                        offset by its WORLD length gives exactly the local vector whose world image
                        is a unit vector.
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
                        float theta = dot(offset_world / dst,normalize(-lights[li].direction));
                        if (theta < lights[li].cos_angle){
                            continue;
                        }
                        attenuation *= clamp((theta - lights[li].cos_angle) / 0.15,0.0,1.0);
                    }
                    attenuation *= light_march(p,dir_local,dst,num_light_steps);
                    scattered += lights[li].color * attenuation;
                }
                scattered = min(scattered,vec3(max_radiance));
            }

            vec3 source = emitted + scattered * smoke_albedo * smokiness;
            radiance += density * step_size * transmittance * source;
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

    //The pipeline blends with GL_SRC_ALPHA / GL_ONE_MINUS_SRC_ALPHA (Renderer::SetOpenGLState), so
    //whatever we write here is multiplied by alpha again. `radiance` is already an absolute amount
    //of light and already carries its colour, so divide alpha back out to land on exactly
    //`radiance` over the background - which is what lets the core run past 1 and bloom while the
    //thin smoke at the edge stays a gentle veil.
    color = vec4(radiance / max(alpha,0.0001),alpha);
}
