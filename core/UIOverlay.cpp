#include "UIOverlay.h"
#include "Shader.h"
#include "Debug.h"
#include "File.h"
#include <string.h>
#include <stddef.h>
//sqrtf, for AddLine's direction. The only maths in this file that is not add and multiply.
#include <math.h>

static Debugger* debug = new Debugger("UIOverlay", DEBUG_INFO);

/*
    GLES has no Direct State Access at any version, including 3.2 - no glCreateBuffers, no
    glNamedBufferData, no glVertexArrayAttribFormat. The Android port shims this in
    android_core/Mesh.cpp and these two helpers are the same shim for this file's much smaller
    surface: bind, then call, so the format latches into whatever is currently bound.

    Nothing in THIS tree defines __ANDROID__, so this arm is never built here - but it is not
    speculative: the same file compiles for aarch64 with the NDK and RUNS, on the Mali-T720, in
    the Android port at C:/code/android. See docs/ui_overlay_plan.md section 15.

    Measured while proving it, and the reason this shim is not optional: not one of glCreateBuffers,
    glNamedBufferData, glVertexArrayAttribFormat, glVertexArrayVertexBuffer, glEnableVertexArrayAttrib,
    glBindTextureUnit, glCreateTextures, glCreateVertexArrays, glTextureStorage2D or
    glTextureSubImage2D appears anywhere in GLES3/gl3.h, gl31.h or gl32.h. DSA is absent at every
    GLES version, exactly as section 1 claimed.
*/
#if defined(__ANDROID__)
static void UIUploadBuffer(GLuint buffer, size_t bytes, const void* data){
    glBindBuffer(GL_ARRAY_BUFFER,buffer);
    glBufferData(GL_ARRAY_BUFFER,bytes,data,GL_DYNAMIC_DRAW);
}
#else
static void UIUploadBuffer(GLuint buffer, size_t bytes, const void* data){
    glNamedBufferData(buffer,bytes,data,GL_DYNAMIC_DRAW);
}
#endif

bool UIOverlay::InitBuffers(){
#if defined(__ANDROID__)
    glGenBuffers(1,&vbo);
    glGenVertexArrays(1,&vao);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER,vbo);

    #define UI_ATTRIB(loc,count,type,norm,field) \
        glEnableVertexAttribArray(loc); \
        glVertexAttribPointer(loc,count,type,norm,sizeof(ui_vertex),(void*)offsetof(ui_vertex,field));

    UI_ATTRIB(0,2,GL_FLOAT,GL_FALSE,pos)
    UI_ATTRIB(1,2,GL_FLOAT,GL_FALSE,uv)
    UI_ATTRIB(2,2,GL_FLOAT,GL_FALSE,local)
    UI_ATTRIB(3,2,GL_FLOAT,GL_FALSE,half_extent)
    UI_ATTRIB(4,1,GL_FLOAT,GL_FALSE,radius)
    UI_ATTRIB(5,1,GL_FLOAT,GL_FALSE,outline)
    UI_ATTRIB(6,1,GL_FLOAT,GL_FALSE,distance_scale)
    //GL_TRUE: the four bytes arrive in the shader as a 0..1 vec4 rather than 0..255.
    UI_ATTRIB(7,4,GL_UNSIGNED_BYTE,GL_TRUE,color)
    UI_ATTRIB(8,1,GL_FLOAT,GL_FALSE,sprite)
    #undef UI_ATTRIB

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER,0);
#else
    glCreateBuffers(1,&vbo);
    glCreateVertexArrays(1,&vao);
    glVertexArrayVertexBuffer(vao,0,vbo,0,sizeof(ui_vertex));

    #define UI_ATTRIB(loc,count,type,norm,field) \
        glEnableVertexArrayAttrib(vao,loc); \
        glVertexArrayAttribFormat(vao,loc,count,type,norm,offsetof(ui_vertex,field)); \
        glVertexArrayAttribBinding(vao,loc,0);

    UI_ATTRIB(0,2,GL_FLOAT,GL_FALSE,pos)
    UI_ATTRIB(1,2,GL_FLOAT,GL_FALSE,uv)
    UI_ATTRIB(2,2,GL_FLOAT,GL_FALSE,local)
    UI_ATTRIB(3,2,GL_FLOAT,GL_FALSE,half_extent)
    UI_ATTRIB(4,1,GL_FLOAT,GL_FALSE,radius)
    UI_ATTRIB(5,1,GL_FLOAT,GL_FALSE,outline)
    UI_ATTRIB(6,1,GL_FLOAT,GL_FALSE,distance_scale)
    UI_ATTRIB(7,4,GL_UNSIGNED_BYTE,GL_TRUE,color)
    UI_ATTRIB(8,1,GL_FLOAT,GL_FALSE,sprite)
    #undef UI_ATTRIB
#endif
    return true;
}

/*
    The atlas texture, built by hand rather than through core/Texture.

    Texture::Create2D refuses anything but GL_RGB8/GL_RGBA8 outright, and hardcodes NEAREST
    filtering with REPEAT wrapping. All three are wrong here: the field is single channel, it must
    filter LINEAR (a distance field interpolates - that is the entire point of using one), and it
    must CLAMP so a sample at the atlas edge cannot wrap to the far side. Bending Texture to cover
    this would be a larger change than the six calls below.

    No mipmaps, deliberately. A mipmap of a distance field averages distances, which is not the
    distance of the averaged shape; at the minification this text sees it costs more than the
    aliasing it would prevent.
*/
bool UIOverlay::InitFontTexture(const uint8_t* pixels){
    //Rows are single-channel and the atlas width is arbitrary, so the default 4-byte unpack
    //alignment would skew every row of an atlas whose width is not a multiple of 4. It happens
    //not to be today, which is exactly why this is easy to leave out and horrible to find later.
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);

#if defined(__ANDROID__)
    glGenTextures(1,&atlas_tex);
    glBindTexture(GL_TEXTURE_2D,atlas_tex);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexStorage2D(GL_TEXTURE_2D,1,GL_R8,font.atlas_w,font.atlas_h);
    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,font.atlas_w,font.atlas_h,GL_RED,GL_UNSIGNED_BYTE,pixels);
    glBindTexture(GL_TEXTURE_2D,0);
#else
    glCreateTextures(GL_TEXTURE_2D,1,&atlas_tex);
    glTextureParameteri(atlas_tex,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTextureParameteri(atlas_tex,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTextureParameteri(atlas_tex,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTextureParameteri(atlas_tex,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTextureStorage2D(atlas_tex,1,GL_R8,font.atlas_w,font.atlas_h);
    glTextureSubImage2D(atlas_tex,0,0,0,font.atlas_w,font.atlas_h,GL_RED,GL_UNSIGNED_BYTE,pixels);
#endif

    //Back to the default, so nothing downstream inherits a setting it did not ask for.
    glPixelStorei(GL_UNPACK_ALIGNMENT,4);
    return true;
}

bool UIOverlay::Init(const char* font_asset, const char* vert_asset, const char* frag_asset){
    size_t sz = 0;
    uint8_t* data = LoadFile(font_asset,&sz);
    if (!data){
        //LoadFile has already named the file on stderr.
        debug->Err("UIOverlay: no font, the overlay will draw nothing\n");
        return false;
    }
    if (sz < sizeof(ui_font_header)){
        debug->Err("UIOverlay: %s is %zu bytes, too short to be a font\n",font_asset,sz);
        return false;
    }
    memcpy(&font,data,sizeof(ui_font_header));

    //The same check tools/fontbake ran before writing, so a file that survived the trip is
    //rejected here rather than sized into a GL allocation.
    if (!UIFontHeaderIsValid(font,sz)){
        debug->Err("UIOverlay: %s is not a valid font (magic/version/size disagree)\n",font_asset);
        return false;
    }

    //Borrowed from the file layer, which owns it for the life of the process (see File.h) - so
    //this is a pointer, not a copy, and it is what lets a context loss be recovered from without
    //touching the disk again.
    atlas_pixels = data + font.pixel_offset;
    vert_name    = vert_asset;
    frag_name    = frag_asset;

    if (!CreateGPUObjects()){
        return false;
    }

    debug->Ok("UIOverlay ready: %s, %ux%u atlas, em %.1f px\n",
              font_asset,font.atlas_w,font.atlas_h,font.em_px);
    return true;
}

bool UIOverlay::CreateGPUObjects(){
    //Reused rather than replaced, and that is the point: Shader::Build assigns a fresh progid and
    //does NOT delete the previous one (only Shader::Reload does). After a context loss the old
    //program id is meaningless, so deleting it would at best do nothing and at worst name a
    //program the NEW context has since handed to somebody else.
    if (!shader){
        shader = new Shader();
    }
    if (!shader->Build(vert_name.c_str(),frag_name.c_str())){
        debug->Err("UIOverlay: could not build %s + %s\n",vert_name.c_str(),frag_name.c_str());
        delete shader;
        shader = NULL;
        return false;
    }

    InitBuffers();
    InitFontTexture(atlas_pixels);

    f_ready = true;
    return true;
}

bool UIOverlay::ReUploadGPUObjects(){
    //Never built, or built and failed. Rebuilding from here would mean inventing the half of
    //Init that reads the font, so say so and stay inert instead.
    if (!atlas_pixels){
        debug->Warn("UIOverlay::ReUploadGPUObjects: nothing to rebuild, Init never succeeded\n");
        return false;
    }

    /*
        FORGOTTEN, NOT DELETED, and this is the whole subtlety of the function.

        These names died with the context that issued them. Calling glDeleteBuffers or
        glDeleteTextures on them now does not free anything - the objects are already gone - and
        the names are live in the NEW context, where the driver is free to have reissued them to
        somebody else's buffer. So the delete would be a no-op on a good day and would destroy an
        unrelated object on a bad one, from a line that reads like cleanup.

        Dropping them on the floor is correct because there is no floor to drop them on: the
        context took everything with it. The Android port's Mesh::ReUploadMeshData zeroes vbo/vao
        for exactly this reason.
    */
    atlas_tex = 0;
    vbo       = 0;
    vao       = 0;
    f_ready   = false;

    //Same reasoning one level up: the Shader object survives, its program did not. Build() will
    //overwrite progid without touching the old value.
    if (!CreateGPUObjects()){
        debug->Err("UIOverlay::ReUploadGPUObjects: rebuild failed, the overlay will draw nothing\n");
        return false;
    }

    //Last frame's quads referred to a buffer that no longer exists, and the surface is very
    //probably a different size than it was. Begin() clears this anyway; doing it here means a
    //Draw() before the next Begin() draws nothing rather than a stale batch.
    vertices.clear();

    debug->Ok("UIOverlay: GPU objects rebuilt after context loss\n");
    return true;
}

void UIOverlay::Begin(int w, int h){
    screen_w = (w > 0) ? w : 1;
    screen_h = (h > 0) ? h : 1;
    //clear(), not a fresh vector: the capacity earned on frame one is kept for every frame after,
    //so a steady-state overlay does no allocation at all.
    vertices.clear();
}

/*
    Six vertices - two triangles, no index buffer.

    An EBO would save a third of the vertex data and is not worth the object: a busy overlay is a
    couple of hundred quads, so the saving is tens of kilobytes a frame against a buffer that has
    to be created, bound and kept in step with the VAO on two platforms.

    --- THE SHAPE IS NOT THE QUAD, AND THAT IS THE WHOLE POINT OF THIS OVERLOAD ------------------
    `min/max` is the GEOMETRY rasterised; `shape_min/shape_max` is the rectangle the fragment
    shader's distance field describes. They are usually the same and AddQuad below passes them
    that way, but the two cases that need them apart are both real:

      - AN OUTLINE straddles the edge, so half of it falls outside the rect asked for. The quad
        grows to cover that half; the shape must not, or the outline moves with it.

      - A CELL OF A NINE-SLICE is one of nine quads that together make ONE rectangle. The shader
        antialiases whatever shape it is given over one pixel, so a cell given itself fades out
        along the INTERIOR cuts too - where there is nothing to antialias and a neighbour is about
        to be drawn. That pixel is rasterised by exactly one of the two cells (the fill rule gives
        it to whichever contains its centre) and the other never touches it, so nothing fills the
        coverage the ramp gave away and the scene behind shows through.

        The leak is min(frac,1-frac) of the cut's position: nil when a cut lands on a pixel
        boundary, up to half the background when it lands on a pixel centre. That is a SEAM that
        comes and goes with the window size, which is exactly how it was found - bomber's menu
        panel at 1280x800 puts its x cuts at .6 and .4 of a pixel and its y cuts on whole ones, so
        it seamed vertically and not horizontally.

        Handing every cell the WHOLE panel as its shape puts the distance field far inside at
        every interior cut - alpha 1, no ramp - while the panel's outer boundary still gets its
        one correct ramp. The cells keep their own geometry, which is what carries the UVs.
*/
void UIOverlay::AddQuadShaped(vec2 min, vec2 max, vec2 shape_min, vec2 shape_max,
                              vec2 uv0, vec2 uv1,
                              float radius, float outline, float distance_scale, uint32_t color,
                              float sprite){
    if (!f_ready){
        return;
    }
    //A zero or inverted rect has no pixels and a negative half-extent would make the distance
    //field nonsense rather than empty, so it is dropped here instead of drawn wrong. Both rects
    //are checked: an empty QUAD rasterises nothing, an empty SHAPE is a field with no inside.
    if ((max.x <= min.x) || (max.y <= min.y)){
        return;
    }
    if ((shape_max.x <= shape_min.x) || (shape_max.y <= shape_min.y)){
        return;
    }

    //From the SHAPE, not the quad - see the header note. Everything the distance field is built
    //from is measured against the rectangle being described, and only `corner_pos` below comes
    //from the geometry.
    vec2 half   = vec2((shape_max.x - shape_min.x) * 0.5f,(shape_max.y - shape_min.y) * 0.5f);
    vec2 centre = vec2(shape_min.x + half.x,shape_min.y + half.y);

    //A radius past half the shorter side would turn the rounded-box distance inside out. Clamping
    //means "very round" degrades into a capsule and then a circle, which is what a caller asking
    //for a huge radius meant.
    float max_radius = (half.x < half.y) ? half.x : half.y;
    if (radius > max_radius){
        radius = max_radius;
    }
    if (radius < 0.0f){
        radius = 0.0f;
    }

    const vec2 corner_pos[4] = {
        vec2(min.x,min.y), vec2(max.x,min.y), vec2(max.x,max.y), vec2(min.x,max.y)
    };
    const vec2 corner_uv[4] = {
        vec2(uv0.x,uv0.y), vec2(uv1.x,uv0.y), vec2(uv1.x,uv1.y), vec2(uv0.x,uv1.y)
    };
    //Counter-clockwise in a top-left origin is clockwise on screen; culling is off for this pass
    //(see Draw) so neither winding matters, and this order is simply the one that reads as a loop.
    const int order[6] = {0,1,2, 0,2,3};

    for (int i = 0; i < 6; i++){
        int c = order[i];
        ui_vertex v;
        v.pos            = corner_pos[c];
        v.uv             = corner_uv[c];
        v.local          = vec2(corner_pos[c].x - centre.x,corner_pos[c].y - centre.y);
        v.half_extent    = half;
        v.radius         = radius;
        v.outline        = outline;
        v.distance_scale = distance_scale;
        v.sprite         = sprite;
        v.color          = color;
        vertices.push_back(v);
    }
}

//The ordinary case: the quad IS the shape.
void UIOverlay::AddQuad(vec2 min, vec2 max, vec2 uv0, vec2 uv1,
                        float radius, float outline, float distance_scale, uint32_t color,
                        float sprite){
    AddQuadShaped(min,max,min,max,uv0,uv1,radius,outline,distance_scale,color,sprite);
}

//The atlas's all-inside texel, as a UV. Both corners are the same point, so every fragment of an
//untextured quad samples exactly it and the glyph term is a constant far inside - see the shader.
static vec2 SolidUV(const ui_font_header& f){
    return vec2(((float)f.solid_x + 0.5f) / (float)f.atlas_w,
                ((float)f.solid_y + 0.5f) / (float)f.atlas_h);
}

//`shape_min/shape_max` is the rectangle the distance field describes, which for a nine-slice cell
//is the whole panel rather than the cell - see AddQuadShaped for why that is not the same thing.
void UIOverlay::AddRectShaped(vec2 min, vec2 max, vec2 shape_min, vec2 shape_max,
                              float radius, uint32_t color){
    if (!f_ready){
        return;
    }
    vec2 solid = SolidUV(font);
    //distance_range_px as the scale is not arbitrary: it has to be big enough that the solid
    //texel's distance, (onedge - 1) * scale, lands further inside than the one-pixel coverage
    //ramp, or a filled rect would come out faintly translucent. A whole field's range is.
    AddQuadShaped(min,max,shape_min,shape_max,solid,solid,radius,0.0f,font.distance_range_px,
                  color,0.0f);
}

void UIOverlay::AddRect(vec2 min, vec2 max, float radius, uint32_t color){
    AddRectShaped(min,max,min,max,radius,color);
}

void UINineSliceRegions(vec2 min, vec2 max, const ui_nine_inset& inset,
                        vec2* out_min, vec2* out_max){
    float w = max.x - min.x;
    float h = max.y - min.y;

    float l = inset.left;
    float r = inset.right;
    float t = inset.top;
    float b = inset.bottom;

    /*
        Shrink opposing insets TOGETHER when they do not fit, keeping their ratio.

        Clamping them one at a time instead would move the panel's visual centre as it narrowed,
        because whichever inset was clamped first would keep its full size while the other gave way.
        Scaling both by the same factor keeps a symmetric frame symmetric all the way down to zero
        width, which is what a panel animating open looks like.
    */
    if ((l + r) > w && (l + r) > 0.0f){
        float k = w / (l + r);
        l *= k;
        r *= k;
    }
    if ((t + b) > h && (t + b) > 0.0f){
        float k = h / (t + b);
        t *= k;
        b *= k;
    }

    //The four cut lines, in order, on each axis. The middle span is whatever is left over and may
    //legitimately be zero - a panel exactly as wide as its own corners has no middle column.
    const float xs[4] = {min.x, min.x + l, max.x - r, max.x};
    const float ys[4] = {min.y, min.y + t, max.y - b, max.y};

    for (int row = 0; row < 3; row++){
        for (int col = 0; col < 3; col++){
            int i = row * 3 + col;
            out_min[i] = vec2(xs[col],    ys[row]);
            out_max[i] = vec2(xs[col + 1],ys[row + 1]);
        }
    }
}

void UIOverlay::AddNineSliceDebug(vec2 min, vec2 max, const ui_nine_inset& inset, uint8_t alpha){
    if (!f_ready){
        return;
    }
    vec2 rmin[UI_NINE_COUNT];
    vec2 rmax[UI_NINE_COUNT];
    UINineSliceRegions(min,max,inset,rmin,rmax);

    //Indexed the same way as the regions - row-major from the top-left. Corners red, the edges
    //that stretch horizontally green, the ones that stretch vertically blue, the centre grey.
    const uint32_t role[UI_NINE_COUNT] = {
        UIColor(224, 82, 82,alpha), UIColor( 96,200,104,alpha), UIColor(224, 82, 82,alpha),
        UIColor( 86,150,236,alpha), UIColor(150,150,160,alpha), UIColor( 86,150,236,alpha),
        UIColor(224, 82, 82,alpha), UIColor( 96,200,104,alpha), UIColor(224, 82, 82,alpha),
    };

    for (int i = 0; i < UI_NINE_COUNT; i++){
        //A degenerate region is skipped rather than drawn: a zero-width middle column is a
        //legitimate outcome of the clamp above, and a zero-area quad would still cost six
        //vertices and an antialiasing ramp that can leave a faint line where nothing should be.
        if (rmax[i].x - rmin[i].x <= 0.0f || rmax[i].y - rmin[i].y <= 0.0f){
            continue;
        }
        //Radius 0: each region is a plain rectangle. Rounded corners on a nine-slice come from the
        //ARTWORK in the corner regions, never from the geometry - rounding these would round the
        //interior cuts too, and leave gaps along every seam.
        //
        //The SHAPE is the whole panel, so the interior cuts get no coverage ramp and the regions
        //meet exactly - the same fix the themed path below needs, and it belongs here too or the
        //fallback stops being the faithful stand-in this function exists to be. See AddQuadShaped.
        AddRectShaped(rmin[i],rmax[i],min,max,0.0f,role[i]);
    }
}

void UIOverlay::SetThemeTexture(uint32_t tex_id, int w, int h){
    theme_tex = (GLuint)tex_id;
    theme_w   = w;
    theme_h   = h;
}

//`shape_min/shape_max` is the rectangle the distance field clips against, which for a nine-slice
//cell is the whole panel rather than the cell - see AddQuadShaped for why that is not the same
//thing, and for the seam that having them the same produced.
void UIOverlay::AddSpriteShaped(vec2 min, vec2 max, vec2 shape_min, vec2 shape_max,
                                int src_x, int src_y, int src_w, int src_h, uint32_t color){
    if (!f_ready || !theme_tex || theme_w <= 0 || theme_h <= 0){
        return;
    }
    if (src_w <= 0 || src_h <= 0){
        return;
    }

    /*
        HALF-TEXEL INSET ON ALL FOUR EDGES.

        A sprite's UVs address the CENTRES of its outermost texels, not the boundaries between them.
        Sampling exactly on a boundary is a coin toss between this sprite and whatever the packer
        put next to it, and with two pixels of padding that neighbour is transparent - so a sprite
        drawn without this gets a faint transparent seam along its edges, worst exactly where a
        nine-slice repeats an edge region.
    */
    float u0 = ((float)src_x + 0.5f) / (float)theme_w;
    float v0 = ((float)src_y + 0.5f) / (float)theme_h;
    float u1 = ((float)(src_x + src_w) - 0.5f) / (float)theme_w;
    float v1 = ((float)(src_y + src_h) - 0.5f) / (float)theme_h;

    //radius 0, outline 0: a sprite is a plain rectangle. Any rounding belongs to the ARTWORK, and
    //rounding the quad as well would cut the artwork's own corners off. distance_scale 0 keeps the
    //glyph term out of it - see the shader.
    AddQuadShaped(min,max,shape_min,shape_max,vec2(u0,v0),vec2(u1,v1),0.0f,0.0f,0.0f,color,1.0f);
}

void UIOverlay::AddSprite(vec2 min, vec2 max, int src_x, int src_y, int src_w, int src_h,
                          uint32_t color){
    AddSpriteShaped(min,max,min,max,src_x,src_y,src_w,src_h,color);
}

void UIOverlay::AddNineSliceSprite(vec2 min, vec2 max, int src_x, int src_y, int src_w, int src_h,
                                   const ui_nine_inset& inset, uint32_t color){
    if (!f_ready || !theme_tex){
        return;
    }

    /*
        The DESTINATION regions come from the same function the debug colours use, so the artwork
        lands exactly where the coloured blocks did - including the clamp that stops a panel
        narrower than its own insets folding its corners through each other.

        The SOURCE regions are cut with the insets UNCLAMPED, because the source never shrinks: the
        sprite is whatever size the artist drew. When the destination clamps, a full-size corner is
        drawn into a smaller box and squashes - which is correct, and the only sane thing left at a
        size the art was never meant for.
    */
    vec2 dmin[UI_NINE_COUNT];
    vec2 dmax[UI_NINE_COUNT];
    UINineSliceRegions(min,max,inset,dmin,dmax);

    const float sxs[4] = {
        (float)src_x,
        (float)src_x + inset.left,
        (float)(src_x + src_w) - inset.right,
        (float)(src_x + src_w)
    };
    const float sys[4] = {
        (float)src_y,
        (float)src_y + inset.top,
        (float)(src_y + src_h) - inset.bottom,
        (float)(src_y + src_h)
    };

    for (int row = 0; row < 3; row++){
        for (int col = 0; col < 3; col++){
            int i = row * 3 + col;
            //A zero-width middle column is a legitimate outcome of the clamp; a zero-height source
            //row is what an inset of 0 on that axis means, which is a three-slice. Both are skipped
            //rather than drawn as degenerate quads.
            if ((dmax[i].x - dmin[i].x) <= 0.0f || (dmax[i].y - dmin[i].y) <= 0.0f){
                continue;
            }
            int sx = (int)sxs[col];
            int sy = (int)sys[row];
            int sw = (int)(sxs[col + 1] - sxs[col]);
            int sh = (int)(sys[row + 1] - sys[row]);
            if (sw <= 0 || sh <= 0){
                continue;
            }
            /*
                THE SHAPE IS THE WHOLE PANEL, not the cell, and this one argument is the difference
                between a clean nine-slice and a one-pixel seam down every interior cut. The cells
                still carry their own geometry - that is what holds the UVs - but the distance
                field they are clipped by describes the rectangle they jointly make up, so only
                its outer boundary gets a coverage ramp. See AddQuadShaped for the measurement.
            */
            AddSpriteShaped(dmin[i],dmax[i],min,max,sx,sy,sw,sh,color);
        }
    }
}

void UIOverlay::AddRectOutline(vec2 min, vec2 max, float radius, float thickness, uint32_t color){
    if (!f_ready){
        return;
    }
    if (thickness <= 0.0f){
        return;
    }
    /*
        The outline straddles the edge, so half of it falls OUTSIDE the rect given. The quad has
        to be grown to cover that half plus the antialiasing ramp, or the outline is cut off flat
        along its outer side - which reads as a rendering bug and is really a too-small quad.
    */
    float grow = thickness * 0.5f + 1.0f;
    vec2 gmin  = vec2(min.x - grow,min.y - grow);
    vec2 gmax  = vec2(max.x + grow,max.y + grow);

    //The distance field must still describe the ORIGINAL rectangle, so the quad is bigger than
    //the shape: AddQuad derives half_extent from the quad, so the grown size is compensated here
    //by passing the geometry through a quad whose centre matches and whose half-extent is grown.
    //Shrinking the local space back is what keeps the outline on the edge the caller asked for.
    vec2 half   = vec2((max.x - min.x) * 0.5f,(max.y - min.y) * 0.5f);
    vec2 centre = vec2(min.x + half.x,min.y + half.y);
    vec2 solid  = SolidUV(font);

    float max_radius = (half.x < half.y) ? half.x : half.y;
    if (radius > max_radius){
        radius = max_radius;
    }

    const vec2 corner_pos[4] = {
        vec2(gmin.x,gmin.y), vec2(gmax.x,gmin.y), vec2(gmax.x,gmax.y), vec2(gmin.x,gmax.y)
    };
    const int order[6] = {0,1,2, 0,2,3};
    for (int i = 0; i < 6; i++){
        int c = order[i];
        ui_vertex v;
        v.pos            = corner_pos[c];
        v.uv             = solid;
        v.local          = vec2(corner_pos[c].x - centre.x,corner_pos[c].y - centre.y);
        v.half_extent    = half;                 //the SHAPE's half size, not the quad's
        v.radius         = radius;
        v.outline        = thickness * 0.5f;
        //Explicit although the member defaults to it now: this emitter sets every field by hand,
        //and a list with one silently missing is exactly how that turned into a bug.
        v.sprite         = 0.0f;
        v.distance_scale = font.distance_range_px;
        v.color          = color;
        vertices.push_back(v);
    }
}

//See the header for why a rotated quad needs nothing from the shader. This function is that
//comment made real: `local` is built from the UNROTATED corners and `pos` from the rotated ones.
void UIOverlay::AddLine(vec2 a, vec2 b, float thickness, uint32_t color){
    if (!f_ready){
        return;
    }
    if (thickness <= 0.0f){
        return;
    }

    float dx  = b.x - a.x;
    float dy  = b.y - a.y;
    float len = sqrtf(dx * dx + dy * dy);

    //The unit direction, and the identity for a zero-length stroke - which then draws its cap as
    //a circle rather than dividing by zero. See the header.
    float ux = 1.0f;
    float uy = 0.0f;
    if (len > 0.0f){
        ux = dx / len;
        uy = dy / len;
    }

    /*
        Half the SHAPE, in its own frame: half the length plus half the thickness along the
        stroke, half the thickness across it.

        The `+ thickness * 0.5f` is what puts the round caps ON the endpoints rather than inset
        from them. Without it a stroke from a to b stops half a thickness short at each end,
        which is invisible on a hairline and obviously wrong on a ten-pixel one - and a tick
        drawn as two strokes would come apart at the joint.
    */
    float hx = len * 0.5f + thickness * 0.5f;
    float hy = thickness * 0.5f;

    vec2 half   = vec2(hx,hy);
    vec2 centre = vec2((a.x + b.x) * 0.5f,(a.y + b.y) * 0.5f);
    vec2 solid  = SolidUV(font);

    const vec2 corner_local[4] = {
        vec2(-hx,-hy), vec2(hx,-hy), vec2(hx,hy), vec2(-hx,hy)
    };
    const int order[6] = {0,1,2, 0,2,3};

    for (int i = 0; i < 6; i++){
        int c = order[i];
        ui_vertex v;
        //The whole trick, in two lines: the position is the corner rotated onto the stroke's
        //axis and carried to its centre, while `local` just below stays the corner it was.
        v.pos            = vec2(centre.x + corner_local[c].x * ux - corner_local[c].y * uy,
                                centre.y + corner_local[c].x * uy + corner_local[c].y * ux);
        v.uv             = solid;
        v.local          = corner_local[c];
        v.half_extent    = half;
        //Exactly the clamp limit, hy being the shorter half by construction - so the rounded box
        //IS a capsule and the two ends are semicircles.
        v.radius         = hy;
        v.outline        = 0.0f;
        v.sprite         = 0.0f;
        v.distance_scale = font.distance_range_px;
        v.color          = color;
        vertices.push_back(v);
    }
}

vec2 UIOverlay::MeasureText(const char* text, float size_px) const{
    if (!text){
        return vec2(0.0f,0.0f);
    }
    float scale = UIFontDistanceScale(font,size_px);   //size_px / em_px
    int count = 0;
    for (const char* c = text; *c; c++){
        count++;
    }
    return vec2((float)count * font.advance_px * scale,font.line_height_px * scale);
}

void UIOverlay::AddText(const char* text, vec2 pos, float size_px, uint32_t color, int align){
    if (!f_ready || !text || (size_px <= 0.0f)){
        return;
    }
    float scale = UIFontDistanceScale(font,size_px);
    if (scale <= 0.0f){
        return;
    }

    float width = MeasureText(text,size_px).x;
    float pen_x = pos.x;
    if (align == UI_ALIGN_CENTER){
        pen_x -= width * 0.5f;
    }else if (align == UI_ALIGN_RIGHT){
        pen_x -= width;
    }

    //Everything a glyph needs, scaled once rather than per character.
    float cell_w   = (float)font.cell_w * scale;
    float cell_h   = (float)font.cell_h * scale;
    float origin_x = font.origin_x * scale;
    float origin_y = font.origin_y * scale;
    float advance  = font.advance_px * scale;
    //The field was measured in bake pixels, so drawing bigger spreads it over proportionally more
    //screen pixels. Miss this multiply and the antialiasing is too soft when magnified and too
    //hard when minified - which reads as a bad font rather than a missing scale.
    float dscale   = font.distance_range_px * scale;

    float inv_w = 1.0f / (float)font.atlas_w;
    float inv_h = 1.0f / (float)font.atlas_h;

    for (const char* c = text; *c; c++){
        uint32_t cell_x = 0, cell_y = 0;
        //Through the shared helper, so the layout unpacks the grid by the same rule the baker
        //packed it with - see core/UIFont.h. An unknown character advances the pen and draws
        //nothing, which is what keeps a stray byte from shifting the rest of the line.
        if (UIFontCell(font,(uint32_t)(uint8_t)*c,&cell_x,&cell_y)){
            vec2 min = vec2(pen_x - origin_x,pos.y - origin_y);
            vec2 max = vec2(min.x + cell_w,min.y + cell_h);
            vec2 uv0 = vec2((float)(cell_x * font.cell_w) * inv_w,
                            (float)(cell_y * font.cell_h) * inv_h);
            vec2 uv1 = vec2((float)((cell_x + 1) * font.cell_w) * inv_w,
                            (float)((cell_y + 1) * font.cell_h) * inv_h);
            //radius 0 and the quad's own half-extent: the box term is then negative everywhere
            //inside the cell and the glyph decides, except at the cell edge where it clips.
            AddQuad(min,max,uv0,uv1,0.0f,0.0f,dscale,color);
        }
        pen_x += advance;
    }
}

void UIOverlay::Draw(){
    if (!f_ready || vertices.empty()){
        return;
    }

    UIUploadBuffer(vbo,vertices.size() * sizeof(ui_vertex),vertices.data());

    /*
        Renderer::SetOpenGLState runs ONCE, from Renderer::Init - not per frame. So every state
        this changes stays changed for the next frame's scene pass unless it is put back, and
        leaving the depth test off would flatten the whole game one frame later. That is the
        reason for the restore block at the bottom rather than tidiness.
    */
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);

    //The WHOLE window, not Renderer's viewport. That viewport can be a smaller, offset
    //sub-rectangle (apps/tank sets viewport_x), and the overlay is screen furniture that belongs
    //over all of it.
    glViewport(0,0,screen_w,screen_h);

    shader->Use();
    shader->Setvec2("screen_size",vec2((float)screen_w,(float)screen_h));
    shader->Setfloat("onedge",font.onedge);
    //NOT unit 0 - see ATLAS_TEXTURE_UNIT in UIOverlay.h for what binding this every frame
    //did to whichever material the Android renderer had handed unit 0.
    shader->Setint("atlas",ATLAS_TEXTURE_UNIT);
    shader->Setint("theme",THEME_TEXTURE_UNIT);

#if defined(__ANDROID__)
    glActiveTexture(GL_TEXTURE0 + ATLAS_TEXTURE_UNIT);
    glBindTexture(GL_TEXTURE_2D,atlas_tex);
    //See the desktop arm: unit 11 is always bound, to the atlas when there is no theme.
    glActiveTexture(GL_TEXTURE0 + THEME_TEXTURE_UNIT);
    glBindTexture(GL_TEXTURE_2D,theme_tex ? theme_tex : atlas_tex);
    //Left selected, this would make the next unpaired glBindTexture in the frame land here.
    glActiveTexture(GL_TEXTURE0);
#else
    glBindTextureUnit(ATLAS_TEXTURE_UNIT,atlas_tex);
    /*
        The theme unit is bound EVERY frame, and to the font atlas when there is no theme.

        The fragment shader samples both textures unconditionally - see the note there on why it
        has no branch - so an unbound sampler would read whatever unit 11 last held. Pointing it at
        the atlas costs nothing, because `sprite` is 0 on every quad in that case and the result is
        multiplied out anyway. What it buys is that there is no state in which the shader samples
        something undefined.
    */
    glBindTextureUnit(THEME_TEXTURE_UNIT,theme_tex ? theme_tex : atlas_tex);
#endif

    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES,0,(GLsizei)vertices.size());
    glBindVertexArray(0);

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
}
