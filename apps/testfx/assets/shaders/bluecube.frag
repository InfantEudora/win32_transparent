#version 460 core

/*
    bluecube2 - the same reference as bluecube.frag, built from one idea instead of thirty knobs.

    Reference: `art_source/testfx_reference/` - four stills of a slowly turning frosted cube with a
    light inside it, standing in blue haze on a dark floor. Reference only, never shipped.

    --- THE MODEL --------------------------------------------------------------------------------
    A frosted cube is a block of scattering medium, and a light inside it is a point light in fog.
    What the eye sees at a pixel is the light scattered toward it along the part of the ray that is
    INSIDE the cube - and for a point light in a uniform medium that integral has a closed form
    (LightAlongRay below), so the whole interior costs one atan pair per pixel instead of a step
    loop. Everything the reference shows falls out of that one term:

      - the near face is brightest where the ray passes closest to the light, so the bottom of the
        cube glows and the top falls off, without a hand-drawn gradient;
      - edges nearest the light burn to white because a ray grazing them passes right by it;
      - the far faces are darker because their rays cross less lit medium - the cube reads as a
        solid with depth rather than as a card.

    The AIR is the same medium at a lower density, so the bloom behind the cube, the haze it
    stands in and the lit fog are the same function called on the part of the ray OUTSIDE the
    cube. One light, one scattering function, used twice. The floor is lit by the same light.

    What is NOT here, on purpose: no SDF march, no interior step loop, no separate halo, pool,
    fog-colour and haze systems each with their own knobs. Where bluecube.frag needed a knob to
    reconcile two terms, this file has one term.

    --- THE CUBE TURNS ---------------------------------------------------------------------------
    It is a rotating cube in the reference, so it rotates here: leaned over by `tilt_degrees` and
    then spun about the vertical, so the lean sweeps round and the silhouette walks through the
    poses the frames show (a 22-degree roll in one, corner-down in another). The floor sits just
    under the lowest point that tumble can reach, derived from the tilt rather than dialled in.
    iTime is the simulation tick, so sim_pause freezes it and a screenshot of frame N is
    reproducible.

    --- NATIVE FORM ------------------------------------------------------------------------------
    Own main(), marches the scene's real camera via iCamPos/FxViewRay, so the middle mouse button
    orbits it. Nine knobs, all plain uniforms, all picked up by the Effect panel.

    The frames are shot from LOW, at about the cube's lower third, so the floor is a thin band and
    the top face never shows. The bench's default camera sits above the cube; this is the view the
    colours were matched at:

        camera_set  position [0.35,-0.42,2.45]  look_at [0,-0.12,0]

    Matched by sampling frame 2 and this render at the same spots (face top, face middle, side
    face, background beside the cube, floor, corners) in linear light - the constants below are
    those numbers, not guesses. What is deliberately NOT matched: the frames' bloom sits off to
    the right and their dark frame 1 still has a bright background, both of which are a second
    light behind the cube in the video. One light here, so `glow` walks the cube through the four
    frames and the background follows it.
*/
#include "engine.glsl"

//--- the knobs --------------------------------------------------------------------------------
uniform float spin_rate    = 0.02;                  //turns per second about the vertical
uniform float tilt_degrees = 22.0;                  //how far the cube leans off its spin axis
uniform float glow         = 1.0;                   //the light's strength; ~0.15 is frame 1, ~4 is frame 4
uniform vec3  glow_color   = vec3(0.01,0.55,1.00);  //linear, sampled from frame 2's face: almost no red
uniform float light_height = -0.80;                 //where the light sits: -1 bottom face, 0 centre, +1 top
uniform float frost        = 0.25;                  //how much light still escapes at a grazing angle
uniform float grain        = 1.0;                   //surface grain and specks from iChannel0
uniform float haze         = 0.10;                  //air density: distance fog and how far the bloom reaches
uniform float exposure     = 1.0;

//--- the constants ----------------------------------------------------------------------------
#define TAU          6.28318530718
#define CUBE         0.55       //half-extent
#define LIGHT_RADIUS 0.20       //a point light is infinitely bright on the face it touches; this is not
#define CUBE_SCATTER 0.50       //how much of the light the frost turns toward the eye per unit path
#define CUBE_FILL    0.02       //multiply-scattered light filling the frost evenly, per unit path
#define AIR_SCATTER  0.30       //single scatter in the air, per unit path
#define AIR_RADIUS   1.00       //the air is lit by the cube AND the haze right round it: a ball this big
#define EXTINCTION   2.5        //how fast light dies with distance through the frost, per unit
#define RIM_GAIN     8.0        //how much of the light reaching an edge leaks out of it
#define EDGE_WIDTH   0.012      //the rim line, as a distance in from a face's edge
#define GROUND_GAP   0.05       //floor clearance below the lowest point of the tumble

//Linear, sampled from the frames: near-black overhead, a thin blue band at the horizon, a floor
//that is dark navy rather than lit teal. Most of the blue in the frames is the lit air below,
//not these.
const vec3 SKY_TOP     = vec3(0.0010,0.0020,0.006);
const vec3 SKY_HORIZON = vec3(0.0040,0.0160,0.050);
const vec3 GROUND      = vec3(0.0040,0.0080,0.020);
//Scattered light comes out bluer than the light that went in - the bloom in the frames is bluer
//than the cube that makes it, and the dim top of the cube is blue where the lit bottom is cyan.
const vec3 AIR_TINT    = vec3(0.35,0.60,1.00);

mat3 RotY(float a){
    float s = sin(a), c = cos(a);
    return mat3( c,0.0, -s,
                0.0,1.0,0.0,
                 s,0.0, c);
}

mat3 RotZ(float a){
    float s = sin(a), c = cos(a);
    return mat3( c,  s,0.0,
                -s,  c,0.0,
                0.0,0.0,1.0);
}

/*
    Light scattered toward the eye along ro + rd*t for t in [t0,t1], from a point light at L in a
    uniform medium - the integral of 1/(distance to the light)^2 along the segment, which is
    atan in closed form. `radius` softens the light from a point into a small ball, so a ray
    grazing it tops out at pi/radius instead of going to infinity.

    Unshadowed, and that is the approximation the whole file rests on: the cube does not shade the
    air behind it. It is self-lit, so nothing in the picture can tell.
*/
float LightAlongRay(vec3 ro, vec3 rd, vec3 L, float radius, float t0, float t1, out float h_out){
    vec3 q = L - ro;
    float a = dot(q,rd);
    float h = sqrt(max(dot(q,q) - a * a,0.0) + radius * radius);
    h_out = h;
    return (atan((t1 - a) / h) - atan((t0 - a) / h)) / h;
}

void main(){
    //--- the cube's pose, once ----------------------------------------------------------------
    float tilt = radians(tilt_degrees);
    //Lean first, then spin about the world vertical, so the lean is carried round: a tumble.
    mat3 to_world = RotY(iTime * spin_rate * TAU) * RotZ(tilt);
    mat3 to_object = transpose(to_world);    //a rotation's inverse is its transpose

    vec3 light_obj = vec3(0.0,light_height * CUBE,0.0);
    vec3 light = to_world * light_obj;

    //A cube leaned by `tilt` reaches down cos+sin of its half-extent whichever way it is spun.
    float ground_y = -CUBE * (cos(tilt) + sin(tilt)) - GROUND_GAP;

    //--- the ray ------------------------------------------------------------------------------
    vec3 org = iCamPos;
    vec3 dir = FxViewRay();

    //Analytic ray/box in object space. tn is the entry, tf the exit; the entry normal is the axis
    //whose slab the ray crossed last on the way in.
    vec3 ro = to_object * org;
    vec3 rd = to_object * dir;
    vec3 inv = 1.0 / rd;
    vec3 ta = (-vec3(CUBE) - ro) * inv;
    vec3 tb = ( vec3(CUBE) - ro) * inv;
    vec3 tmin = min(ta,tb);
    vec3 tmax = max(ta,tb);
    float tn = max(tmin.x,max(tmin.y,tmin.z));
    float tf = min(tmax.x,min(tmax.y,tmax.z));
    bool f_cube = (tn < tf) && (tf > 0.0);

    //The floor is a plane, so one division.
    float tg = (dir.y < -0.0001) ? (ground_y - org.y) / dir.y : -1.0;
    bool f_ground = (tg > 0.0) && (!f_cube || tg < tn);
    f_cube = f_cube && !f_ground;

    vec3 color;
    float hit_t;
    if (f_cube){
        hit_t = tn;
        vec3 n_obj = -sign(rd) * step(tmin.yzx,tmin.xyz) * step(tmin.zxy,tmin.xyz);
        vec3 p_obj = ro + rd * tn;
        vec3 n = to_world * n_obj;

        /*
            The light scattered out of the cube toward this pixel: the whole of the effect.

            Two factors. The integral is the 1/d^2 geometry of a point light along the ray. The exp
            is Beer-Lambert - light dies exponentially with the frost it has crossed to reach this
            part of the surface - and it is what makes frame 2: without it the whole face is lit
            with the bottom merely brighter, with it the cyan is packed into the bottom third and
            the top goes dark. Both measured against the frame, neither is a knob.

            The fill is the little light that has bounced around inside and is everywhere in the
            frost, so the far end of the cube is dim rather than black.
        */
        float h;
        float d_light = distance(p_obj,light_obj);
        float inside = glow * CUBE_SCATTER * exp(-d_light * EXTINCTION) *
                       LightAlongRay(ro,rd,light_obj,LIGHT_RADIUS,tn,tf,h);
        float fill   = glow * CUBE_FILL * (tf - tn);

        //Frost: a diffusing surface lets less out sideways, which darkens the silhouette and is
        //what gives the cube thickness instead of a flat emissive face.
        float fresnel = pow(1.0 - clamp(dot(n,-dir),0.0,1.0),3.0);
        inside *= mix(1.0,frost,fresnel);
        fill   *= mix(1.0,frost,fresnel);

        //The face this ray entered, as a 2D coordinate on it - for the edges and the grain.
        vec2 uv = (abs(n_obj.x) > 0.5) ? p_obj.yz : (abs(n_obj.y) > 0.5) ? p_obj.xz : p_obj.xy;
        //Offset per face so the six faces do not share one pattern.
        vec2 nuv = uv + dot(n_obj,vec3(1.0,2.0,3.0)) * 0.31;

        //Grain rides the light, so a dark cube shows none and a blown-out one is covered in it
        //(frames 1 and 4). Two scales, because the noise is white and one-texel: fine for the
        //roughness, coarse for the droplet specks - see the speck_scale note in bluecube.frag.
        float g = texture(iChannel0,nuv * 1.6).r - 0.5;
        float s = smoothstep(0.90,0.99,texture(iChannel0,nuv * 0.5).r);
        inside *= 1.0 + grain * (0.35 * g + 2.5 * s);

        /*
            The edges. A frosted block's edges catch and leak light, so a thin line along each
            face edge, lit by how much light reaches that part of the surface: white where the
            light is right behind it, a faint cyan seam elsewhere (frame 1's one lit edge).
            Distance in from the face edge, not a bevel in the geometry - the line is the look.
        */
        vec2 e = vec2(CUBE) - abs(uv);
        float rim = (1.0 - smoothstep(0.0,EDGE_WIDTH,e.x)) + (1.0 - smoothstep(0.0,EDGE_WIDTH,e.y));
        //Lit by the irradiance at the edge - the same 1/d^2 and the same extinction as the
        //interior - so the bottom edges burn while the top ones barely register, and the seam
        //walks round the cube with the light as it tumbles instead of outlining it.
        float irradiance = glow * exp(-d_light * EXTINCTION) /
                           (d_light * d_light + LIGHT_RADIUS * LIGHT_RADIUS);
        //In the light's own colour: the tone map below takes a bright enough cyan to white, and a
        //rim that is white to begin with outlines the whole cube in grey, which is the one thing
        //that stops a self-lit object reading as self-lit.
        rim *= 0.005 + irradiance * RIM_GAIN;

        //The fill has bounced around inside the frost, so it carries the same blue shift as the
        //air: the dim top of the cube goes deep blue instead of dim teal, as in frame 2.
        color = glow_color * (inside + rim) + glow_color * AIR_TINT * fill;
    }else if (f_ground){
        hit_t = tg;
        vec3 p = org + dir * tg;

        //Two octaves of the seeded noise, drifting, faded back to their mean with distance
        //because white noise near the horizon aliases into ticks that no mip level averages.
        //Coarse, so linear filtering turns one-texel white noise into soft cloud rather than sand.
        float n0 = texture(iChannel0,p.xz * 0.05 + vec2(iTime * 0.006,0.0)).r;
        float n1 = texture(iChannel0,p.xz * 0.15 - vec2(0.0,iTime * 0.01)).r;
        float bank = mix(mix(n0,n1,0.35),0.5,clamp(tg / 14.0,0.0,1.0));

        //Lit by the same light: the pool under the cube in frames 2 and 3.
        vec3 dl = p - light;
        float pool = glow * 0.04 / (1.0 + dot(dl,dl) * 2.5);
        color = (GROUND + glow_color * pool) * (0.5 + bank);
    }else{
        hit_t = 200.0;
        //The horizon band is thin: the frames are black a quarter of the way up the sky.
        color = mix(SKY_HORIZON,SKY_TOP,1.0 - exp(-max(dir.y,0.0) * 6.0));
    }

    //Distance haze toward the horizon colour - the fog bank the cube stands in. The sky already
    //ends at that colour, so the floor meets it without a seam.
    color = mix(color,SKY_HORIZON,1.0 - exp(-hit_t * haze));

    /*
        The air, lit along the part of the ray that is not inside the cube. This is the bloom
        behind the cube, and it is what stops it sitting on the background like a sticker.

        The SOURCE here is the cube, not the light in it: the frost re-emits from its whole volume,
        so the air is lit from a ball centred on the cube - which is where the frames put the
        bloom, behind the cube's middle rather than under it. Same function as the interior, with
        a source radius a bit bigger than the cube because the haze right round it is lit too.

        The exp is the light being absorbed on its way out through the same air: without it the
        bloom's tail is 1/distance and tints the whole frame instead of pooling round the cube,
        and denser air pulls it in tighter.
    */
    float h;
    float air = glow * AIR_SCATTER * LightAlongRay(org,dir,vec3(0.0),AIR_RADIUS,0.0,hit_t,h);
    air *= exp(-h * (0.5 + haze * 5.0));
    //It is GROUND fog: dense along the floor, thinning with height. Taken at the ray's closest
    //approach to the cube, which is where most of this ray's scatter happens. This is what makes
    //the sky above the cube black while the horizon is a lit bank - in the frames the two differ
    //by twenty times, and a fog of even density puts the same bloom above the cube as beside it.
    float q_y = org.y + dir.y * clamp(dot(-org,dir),0.0,hit_t);
    air *= min(exp(-q_y * 2.0),1.6);
    color += glow_color * AIR_TINT * air;

    //Vignette from the viewport centre - vuv, not gl_FragCoord, so an offset viewport is right.
    vec2 c = (vuv - 0.5) * 2.0 * vec2(iResolution.x / max(iResolution.y,1.0),1.0);
    //Late and steep: the frames are black in the corners and open through the middle, so the
    //bloom beside the cube has to survive it.
    color *= 1.0 - 0.95 * smoothstep(0.6,1.9,length(c));

    //Reinhard so the light can be driven well past 1, then to sRGB - a fullscreen effect owns its
    //whole response curve. The desaturation first is what a sensor does once its channels clip:
    //a strongly coloured light under a per-channel curve alone never reaches white, and the
    //frames' brightest edge and frame 4's whole centre are white with cyan only at their skirts.
    color *= exposure;
    float lum = dot(color,vec3(0.25,0.55,0.20));
    color = mix(color,vec3(lum),0.9 * smoothstep(0.8,3.0,lum));
    color = color / (1.0 + color);
    color = pow(color,vec3(1.0 / 2.2));

    frag_color = vec4(color,1.0);
}
