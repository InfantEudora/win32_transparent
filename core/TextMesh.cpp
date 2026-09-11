#include "TextMesh.h"
#include "Debug.h"

#include <vector>
#include <math.h>

static Debugger* debug = new Debugger("TextMesh",DEBUG_INFO);

/*
    The pen step for one character, and the ONLY place that reads GlyphSet::advance.

    It takes the character it is about to skip past even though a monospaced set ignores it,
    because that is the whole extension point: a proportional font turns GlyphSet::advance into
    an array and this function into a lookup, and nothing else in the file changes.
*/
static float GlyphAdvance(const GlyphSet& glyphs, char c){
    (void)c;
    return glyphs.advance;
}

//Where a line of this width starts, relative to the object's origin.
static float AlignOffset(int align, float line_width){
    if (align == TEXT_ALIGN_CENTER){
        return -line_width * 0.5f;
    }
    if (align == TEXT_ALIGN_RIGHT){
        return -line_width;
    }
    return 0.0f;
}

/*
    Advance total of every line, in em units. Always yields at least one entry, so line N of the
    emit loop below can index this without checking - a string with no newline is one line.

    Carriage returns are skipped rather than advanced past: text arriving from a file or a socket
    may well be CRLF, and drawing a box for the CR would be a bug nobody would think to look for.
*/
static void MeasureLines(const GlyphSet& glyphs, const char* text, std::vector<float>& widths_out){
    float width = 0.0f;
    for (const char* p = text; *p; p++){
        if (*p == '\r'){
            continue;
        }
        if (*p == '\n'){
            widths_out.push_back(width);
            width = 0.0f;
            continue;
        }
        width += GlyphAdvance(glyphs,*p);
    }
    widths_out.push_back(width);
}

bool GlyphSet::IsValid() const{
    //A zero advance would stack every character on the same spot - a set that cannot lay text out
    //is not a usable set, however many meshes it holds.
    if (advance <= 0.0f){
        return false;
    }
    for (int i = 0; i < GLYPHSET_NUM_CHARS; i++){
        if (glyph[i]){
            return true;
        }
    }
    return false;
}

Mesh* GlyphSet::GetGlyph(char c) const{
    //Through unsigned char first: char is signed here, so anything above 0x7F would come out
    //negative and a high-bit byte from a UTF-8 string would index backwards out of the array.
    int index = (int)(unsigned char)c - GLYPHSET_FIRST_CHAR;
    if (index < 0 || index >= GLYPHSET_NUM_CHARS){
        return NULL;
    }
    return glyph[index];
}

vec2 MeasureText(const GlyphSet& glyphs, const char* text, const TextLayout& layout){
    if (!text || !*text){
        return vec2(0.0f,0.0f);
    }

    std::vector<float>line_widths;
    MeasureLines(glyphs,text,line_widths);

    float widest = 0.0f;
    for (size_t i = 0; i < line_widths.size(); i++){
        if (line_widths[i] > widest){
            widest = line_widths[i];
        }
    }

    //Height is line COUNT times line height, not the ink's extent: a label is as tall as the
    //space it reserves, otherwise "score" and "SCORE" would want different boxes.
    return vec2(widest * layout.scale,
                (float)line_widths.size() * glyphs.line_height * layout.scale);
}

Mesh* BuildTextMesh(const GlyphSet& glyphs, const char* text, const TextLayout& layout, Mesh* reuse){
    if (!glyphs.IsValid()){
        //A set that cannot lay anything out is a setup mistake and worth hearing about. Empty
        //TEXT is not - see below, and the header.
        debug->Err("BuildTextMesh: glyph set holds no glyphs, or has no advance\n");
        return NULL;
    }
    if (!text){
        return NULL;
    }

    std::vector<float>line_widths;
    MeasureLines(glyphs,text,line_widths);

    std::vector<vertex>verts;

    int line = 0;
    float pen_x = AlignOffset(layout.align,line_widths[0]);
    float pen_y = 0.0f;

    for (const char* p = text; *p; p++){
        if (*p == '\r'){
            continue;
        }
        if (*p == '\n'){
            line++;
            //MeasureLines guarantees one entry per line, so this cannot run off the end. Checked
            //anyway because the two loops agreeing is an invariant held by reading, and a bad
            //index here would be an out-of-bounds read rather than a wrong-looking label.
            if (line < (int)line_widths.size()){
                pen_x = AlignOffset(layout.align,line_widths[line]);
            }
            pen_y -= glyphs.line_height;
            continue;
        }

        Mesh* glyph = glyphs.GetGlyph(*p);
        if (glyph){
            //The glyph's CPU-side copy. Mesh keeps one for every mesh it uploads, which is what
            //makes baking possible without a single change to the class.
            const std::vector<vertex>& glyph_verts = glyph->GetVertices();
            for (size_t i = 0; i < glyph_verts.size(); i++){
                vertex v = glyph_verts[i];
                v.pos = (v.pos + vec3(pen_x,pen_y,0.0f)) * layout.scale;
                v.matid = layout.matid;
                //Fixed rather than carried over, and not a precaution: GLTFLoader solves tangents
                //from the mesh's UVs and divides by the UV triangle's area, which is ZERO for
                //5149 of the 8264 triangles in data/glyphs_unispace.glb - 62% of them, because a
                //Blender text object gives its extrusion sides no UV area at all. Those tangents
                //arrive as inf or NaN. Text is a plane facing +Z, so +X is the right tangent
                //across all of it, and it is at least a number.
                v.tangent = vec3(1.0f,0.0f,0.0f);
                verts.push_back(v);
            }
        }
        pen_x += GlyphAdvance(glyphs,*p);
    }

    //"", "   " and "\n" are all legitimate things to ask for and none of them has any geometry.
    //Not logged, and `reuse` is deliberately left holding whatever it held: SetMeshData with a
    //count of 0 reaches vertices.at(0) on an empty vector, and this build has exceptions off.
    if (verts.empty()){
        return NULL;
    }

    /*
        UVs normalised across the whole string's bounding box.

        The glyphs' own TEXCOORD_0 is per glyph and means nothing once they are welded together,
        whereas string-relative coordinates are what a shader needs to run a gradient, a sweep or
        a wipe across a word. Computed here rather than during the emit loop because the box is
        not known until the last glyph is placed.
    */
    vec3 bmin = verts[0].pos;
    vec3 bmax = verts[0].pos;
    for (size_t i = 1; i < verts.size(); i++){
        const vec3& p = verts[i].pos;
        if (p.x < bmin.x){ bmin.x = p.x; }
        if (p.y < bmin.y){ bmin.y = p.y; }
        if (p.x > bmax.x){ bmax.x = p.x; }
        if (p.y > bmax.y){ bmax.y = p.y; }
    }
    //A single '.' or '-' is a real string with almost no height, so neither span can be assumed
    //non-zero.
    float span_x = bmax.x - bmin.x;
    float span_y = bmax.y - bmin.y;
    for (size_t i = 0; i < verts.size(); i++){
        verts[i].uv.x = (span_x > 0.0f) ? (verts[i].pos.x - bmin.x) / span_x : 0.0f;
        verts[i].uv.y = (span_y > 0.0f) ? (verts[i].pos.y - bmin.y) / span_y : 0.0f;
    }

    //Into the caller's mesh when there is one. See the header: Mesh has no destructor, so a label
    //that rebuilt itself by delete/new would leak a VBO and a VAO every time it changed.
    Mesh* mesh = reuse ? reuse : new Mesh();
    mesh->SetMeshData(verts.data(),(int)verts.size());
    return mesh;
}
