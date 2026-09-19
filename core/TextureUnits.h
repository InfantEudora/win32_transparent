#ifndef _TEXTUREUNITS_H_
#define _TEXTUREUNITS_H_

/*
    THE TEXTURE UNIT MAP, C++ HALF. The GLSL half is shared_assets/shaders/texture_units.glsl,
    which every shader #includes; THE TWO MUST AGREE, NUMBER FOR NUMBER. Read that file for why
    the order is what it is - the short version is that everything the engine reserves is packed
    into 0-10 so it still exists on a device whose GL_MAX_TEXTURE_IMAGE_UNITS is 16, and the
    materials take everything above.

    These stay defines rather than becoming an enum because the same names appear in GLSL, where
    an enum is not a thing.
*/
#define TEXUNIT_SHADOW            0

//Units CustomShaderPass binds the deferred G-buffer to, so a custom material can see what the
//scene looks like behind it (a raymarched volume needs it to stop the march at solid geometry).
//
//DEPTH IS THE CHANNEL THAT ANSWERS "IS THERE ANYTHING HERE". It is cleared to 1.0, which is
//outside the range any drawn fragment can occupy, so `depth < 1.0` means geometry and nothing
//else does. POSITION AND NORMAL ARE ONLY MEANINGFUL ONCE DEPTH HAS SAID YES.
//
//That matters because the obvious effect reaches for position first, and position cannot tell
//you: its buffer is cleared to (0,0,0,0), and the world origin is a perfectly ordinary place for
//geometry to be - in a game built around the origin it is where all of it is. So a shader that
//samples position to find out how far away the scene is reads "the origin" for the empty sky,
//and the soft-intersection fade everyone writes first dissolves the effect against the
//background. Nor is w a way out: DEFERRED.FRAG WRITES THE MATERIAL'S ALPHA THERE, not a flag.
//(The normal buffer is cleared to (1,0,0,0), a unit +X normal, which is just as legal a value.)
//
//The pattern, as used by shaders/breakout_shield.frag and shaders/raymarch_volume.frag:
//
//    float scene_depth = texture(gbuffer_depth,screen_uv).r;
//    if (scene_depth < 1.0){                                  //something is there
//        vec3 scene_world = texture(gbuffer_position,screen_uv).xyz;
//        ...
//    }
#define TEXUNIT_GBUFFER_DEPTH     1
#define TEXUNIT_GBUFFER_POSITION  2
#define TEXUNIT_GBUFFER_NORMAL    3

//The 2D overlay's two atlases. Declared here rather than only in UIOverlay.h because this map has
//to be readable in one place; UIOverlay.h names these, and carries the long note on why the font
//atlas may not be unit 0.
#define TEXUNIT_UI_ATLAS          4
#define TEXUNIT_UI_THEME          5

//The app's own single unit, for whatever it wants to sample from a custom shader - today the
//ship's 3D cloud noise and bomber's blast noise. UploadMaterials warns if the material textures
//ever reach the reserved run, since the collision would otherwise show up as a volume sampling
//somebody's diffuse map.
#define TEXUNIT_APP_RESERVED      6

//The skybox, and the two optional shadow volumes. Engine-owned, but only some apps use them,
//which is why they sit at the top of the reserved run rather than next to the G-buffer.
//  _SKYBOX_CUBEMAP  the environment map, also used for reflections
//  _CLOUD_SHADOW    volumetric transmittance map, sampled by default.frag's CalcCloudShadow
//  _FIELD_SHADOW    top-down min/max height map, sampled by CalcFieldShadow to shadow point
//                   lights without a cube map
#define TEXUNIT_SKYBOX_CUBEMAP    7
#define TEXUNIT_CLOUD_SHADOW      8
#define TEXUNIT_FIELD_SHADOW      9

/*
    The reduced-resolution custom-shader target, while CompositeLowRes is scaling it back over
    the frame. Nothing else ever samples it.

    A UNIT OF ITS OWN, and not unit 0, which is the obvious choice for a one-off full-screen pass
    and is a trap: UNIT 0 IS THE SHADOW MAP in this engine (DrawFrame binds shadow_tex_id there
    before the colour pass, and default.frag's CalcShadow reads it). Leaving a colour texture
    parked on unit 0 makes every surface in the scene sample its own shadow term out of whatever
    the volume happened to draw - and since that target is cleared to zero, the whole world reads
    as fully shadowed and renders nearly black. Which is what it did, and it looks like a lighting
    bug rather than like a texture binding, because every symptom of it is in the lighting.
*/
#define TEXUNIT_LOWRES_COMPOSITE  10

/*
    MATERIALS TAKE EVERYTHING ABOVE THE RESERVED RUN. UploadMaterials hands units out from
    TEXUNIT_MATERIAL_FIRST upwards, one per diffuse and one per normal map.

    AN INDEX INTO THE SHADER'S material_texture[] IS NOT A TEXTURE UNIT - it is
    unit - TEXUNIT_MATERIAL_FIRST. The SSBO carries the absolute unit, and the shader's switch
    maps it; see SampleMaterialTexture in default.frag, and the note in deferred.frag about what
    a single off-by-one here looked like.

    NUM_MATERIAL_UNITS differs per platform because the ceiling does, and the GLSL half derives
    the same split from GL_ES. 11 + 21 = 32 on the desktop, 11 + 5 = 16 on the device.
*/
#define TEXUNIT_MATERIAL_FIRST    11
#ifdef __ANDROID__
    #define NUM_MATERIAL_UNITS     5
#else
    #define NUM_MATERIAL_UNITS    21
#endif

#endif //_TEXTUREUNITS_H_
