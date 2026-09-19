/*
    THE TEXTURE UNIT MAP. One file, both languages - this is the GLSL half, and the C++ half is
    the TEXUNIT_* block in core/Renderer.h. THE TWO MUST AGREE, NUMBER FOR NUMBER.

    Texture units are global GL state, not per-program: a unit one pass binds is a unit no other
    pass can keep anything in. So they are a fixed, engine-wide layout rather than something each
    pass picks for itself, and the layout lives here so that a shader's layout(binding = N) and
    the C++ glBindTextureUnit(N, ...) that feeds it can never be edited apart.

    WHY THIS ORDER. Everything the engine reserves is packed low, in one contiguous run, and the
    materials - the only part that grows - take everything above it. That is not tidiness: the
    guaranteed floor for GL_MAX_TEXTURE_IMAGE_UNITS is 16 and Android devices really do report
    exactly 16, so an engine unit at 24 is an engine unit that does not exist on the device. The
    old layout had five of them above 23. This one ends its reserved run at 10.

    A UNIT MAY NOT HOLD TWO SAMPLER TYPES IN ONE PROGRAM. That is what forced the reorder rather
    than a renumber: material_texture[] used to be declared at binding 0 with 24 entries, which
    covered units 0-23, so moving the cubemap (samplerCube) or the cloud map (sampler3D) down
    into that range would have put two sampler types on one unit and failed to link. Materials
    now start above every reserved unit, which is why they can be moved at all.
*/
#ifndef TEXTURE_UNITS_GLSL
#define TEXTURE_UNITS_GLSL

//The sun's depth map, read by CalcShadow. Unit 0 because it is the one texture that literally
//every lit fragment in the engine samples.
#define TEXUNIT_SHADOW              0

//The deferred G-buffer, as seen BY A CUSTOM SHADER wanting to know what is behind it (a
//raymarched volume has to stop its march at solid geometry). Depth is the channel that answers
//"is there anything here" - see the long block in core/Renderer.h before using position.
#define TEXUNIT_GBUFFER_DEPTH       1
#define TEXUNIT_GBUFFER_POSITION    2
#define TEXUNIT_GBUFFER_NORMAL      3

//The 2D overlay's two atlases - the SDF font and the theme sheet. Low, and never 0, because the
//overlay rebinds them every frame it draws: whatever else is on those units is gone. See the
//long note on ATLAS_TEXTURE_UNIT in core/UIOverlay.h for the Android bug that came of picking 0.
#define TEXUNIT_UI_ATLAS            4
#define TEXUNIT_UI_THEME            5

//The app's own. One unit, handed to whatever the app wants to sample from a custom shader -
//today the ship's 3D cloud noise and bomber's blast noise. An app needing more than one is the
//point at which this map grows a second, not the point at which an app picks a free-looking
//number for itself.
#define TEXUNIT_APP_RESERVED        6

//The skybox, and the two optional shadow volumes an app may hand the renderer. All three are
//engine-owned but only some apps use them, which is why they sit at the top of the reserved run
//rather than next to the G-buffer.
#define TEXUNIT_SKYBOX_CUBEMAP      7
#define TEXUNIT_CLOUD_SHADOW        8
#define TEXUNIT_FIELD_SHADOW        9

//The reduced-resolution custom-shader target, while CompositeLowRes scales it back over the
//frame. Nothing else ever samples it. NOT unit 0, which was its obvious home and is a trap: unit
//0 is the shadow map, so parking a colour texture there makes every surface read its shadow term
//out of whatever the volume drew, the whole world goes nearly black, and every symptom of it is
//in the lighting rather than in the binding.
#define TEXUNIT_LOWRES_COMPOSITE   10

/*
    MATERIALS TAKE EVERYTHING ABOVE THE RESERVED RUN, and an index into material_texture[] is
    therefore NOT a texture unit - unit = TEXUNIT_MATERIAL_FIRST + index. Both halves of every
    lookup in SampleMaterialTexture are written as literals for that reason; see the block there,
    and see deferred.frag for what a one-off-by-one in exactly this spot looked like.

    The count differs per platform because the ceiling does. GL_ES is defined by the GLSL ES
    compiler itself, so this needs no plumbing from the C++ side and cannot drift from the device
    it is describing.
*/
#define TEXUNIT_MATERIAL_FIRST     11

#ifdef GL_ES
    //16 total is the guaranteed floor and what the test device reports, so 11..15.
    #define NUM_MATERIAL_UNITS      5
#else
    //32 is what every desktop GL 4.5 driver this engine has run on reports, so 11..31. The SPEC
    //minimum is still 16; a desktop that really reported 16 would fail to link this, and would
    //have failed on the old material_texture[24] just as surely.
    #define NUM_MATERIAL_UNITS     21
#endif

#endif //TEXTURE_UNITS_GLSL
