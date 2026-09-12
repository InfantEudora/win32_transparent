/*
    Cloud shape, shared between every shader that has to agree on where the cloud IS.

    Included (see Shader::ResolveIncludes - this engine's GLSL #include) by
    raymarch_volume.frag, which draws the cloud, and cloud_shadow.comp, which builds the
    transmittance map the cloud casts onto the world. Those two MUST evaluate identical density
    or the shadow lands somewhere the cloud is not, and the failure looks like a subtle
    misalignment rather than an obvious bug - which is the whole reason this file exists instead
    of a second copy of density_at().

    Everything here works in a volume's OBJECT space, where the box is the unit cube. The uniforms
    are shape settings shared by every volume in the scene (only the transform is per volume), so
    both including programs set the same values - see ApplicationShip::SetVolumeUniforms and
    ApplicationShip::SetCloudShadowUniforms.

    No #version here: that stays the first line of the file doing the including.
*/

//The tileable 3D worley written by shaders/noise3d.comp, bound by the app at
//TEXUNIT_APP_RESERVED (Renderer.h). All four channels are the same noise at doubling
//frequencies, so density_at builds its FBM from ONE fetch.
layout (binding = 25) uniform sampler3D noise_texture;

uniform float noise_scale = 1.0;        // Noise tiles across the box this many times
uniform vec3 noise_offset = vec3(0);    // Scrolls the noise through the box - the wind
uniform float density_threshold = 0.45; // Noise below this is empty air. What makes clumps
uniform float edge_falloff = 0.15;      // Object-space distance over which the cloud fades out
                                        //  before the box face, so it has no flat sides
uniform float volume_density = 1.0;     // Density per world unit inside the box

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
    every step size built from it - is a real world-space distance, which is what the density
    and Beer's law need. Renormalising the local direction would silently break that on any
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
