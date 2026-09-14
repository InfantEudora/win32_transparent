#include "UIOverlay.h"
#include "Shader.h"
#include "Debug.h"
#include "File.h"
#include <string.h>
#include <stddef.h>

static Debugger* debug = new Debugger("UIOverlay", DEBUG_INFO);

/*
    GLES has no Direct State Access at any version, including 3.2 - no glCreateBuffers, no
    glNamedBufferData, no glVertexArrayAttribFormat. The Android port shims this in
    android_core/Mesh.cpp and these two helpers are the same shim for this file's much smaller
    surface: bind, then call, so the format latches into whatever is currently bound.

    NOT COMPILED OR TESTED IN THIS TREE - nothing here defines __ANDROID__. It is written now
    because the desktop and GLES forms have to say the same thing, and the moment to notice they
    do not is while writing the desktop one. Treat it as intent for the port merge to verify.
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

    shader = new Shader();
    if (!shader->Build(vert_asset,frag_asset)){
        debug->Err("UIOverlay: could not build %s + %s\n",vert_asset,frag_asset);
        delete shader;
        shader = NULL;
        return false;
    }

    InitBuffers();
    InitFontTexture(data + font.pixel_offset);

    f_ready = true;
    debug->Ok("UIOverlay ready: %s, %ux%u atlas, em %.1f px\n",
              font_asset,font.atlas_w,font.atlas_h,font.em_px);
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
*/
void UIOverlay::AddQuad(vec2 min, vec2 max, vec2 uv0, vec2 uv1,
                        float radius, float outline, float distance_scale, uint32_t color){
    if (!f_ready){
        return;
    }
    //A zero or inverted rect has no pixels and a negative half-extent would make the distance
    //field nonsense rather than empty, so it is dropped here instead of drawn wrong.
    if ((max.x <= min.x) || (max.y <= min.y)){
        return;
    }

    vec2 half   = vec2((max.x - min.x) * 0.5f,(max.y - min.y) * 0.5f);
    vec2 centre = vec2(min.x + half.x,min.y + half.y);

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
        v.color          = color;
        vertices.push_back(v);
    }
}

//The atlas's all-inside texel, as a UV. Both corners are the same point, so every fragment of an
//untextured quad samples exactly it and the glyph term is a constant far inside - see the shader.
static vec2 SolidUV(const ui_font_header& f){
    return vec2(((float)f.solid_x + 0.5f) / (float)f.atlas_w,
                ((float)f.solid_y + 0.5f) / (float)f.atlas_h);
}

void UIOverlay::AddRect(vec2 min, vec2 max, float radius, uint32_t color){
    if (!f_ready){
        return;
    }
    vec2 solid = SolidUV(font);
    //distance_range_px as the scale is not arbitrary: it has to be big enough that the solid
    //texel's distance, (onedge - 1) * scale, lands further inside than the one-pixel coverage
    //ramp, or a filled rect would come out faintly translucent. A whole field's range is.
    AddQuad(min,max,solid,solid,radius,0.0f,font.distance_range_px,color);
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
    shader->Setint("atlas",0);

#if defined(__ANDROID__)
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D,atlas_tex);
#else
    glBindTextureUnit(0,atlas_tex);
#endif

    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES,0,(GLsizei)vertices.size());
    glBindVertexArray(0);

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
}
