/*
    Filling a GlyphSet from a .glb - the ONLY part of the text-mesh path that knows what glTF is.

    Kept out of TextMesh.cpp on purpose. BuildTextMesh takes its glyphs as an argument and so
    depends on Mesh and nothing else; put the loader beside it and every app that lays out a
    string would drag in tinygltf. Separate translation unit, separate dependency, and deleting
    this file costs the engine a convenience rather than a feature.
*/
#include "TextMesh.h"
#include "GLTFLoader.h"
#include "Debug.h"

#include <stdio.h>

static Debugger* debug = new Debugger("TextMeshGLB",DEBUG_INFO);

bool LoadGlyphSetFromGLB(GlyphSet& out, const char* filename, float advance, float line_height){
    if (!filename || advance <= 0.0f){
        debug->Err("LoadGlyphSetFromGLB: needs a filename and a positive advance\n");
        return false;
    }

    GLTFLoader loader;

    /*
        GLTFLoader logs a handful of Info lines per node and per mesh. That is proportional to the
        file, which is fine for a tank and is 500-odd lines for a font - enough to bury whatever
        else started up in the same second. Lowered around the load and put back afterwards
        rather than left lowered, because the app may have raised it deliberately.
    */
    Debugger* loader_debug = Debugger::FindHandle("GLTFLoader");
    debug_t restore_level = loader_debug ? (debug_t)loader_debug->level : (debug_t)DEBUG_INFO;
    if (loader_debug){
        loader_debug->SetLevel(DEBUG_WARN);
    }

    loader.LoadGLTFFile(filename);

    int num_loaded = 0;
    for (size_t i = 0; i < loader.node_names.size(); i++){
        const std::string& name = loader.node_names[i];

        //tools/blender_glyph_meshes.py names every node glyph_<4 hex digits>[_<char>], and the
        //optional suffix is why this is a prefix parse rather than a comparison: "glyph_0041_A"
        //and "glyph_007B" both have to land on their codepoint.
        unsigned int codepoint = 0;
        if (sscanf(name.c_str(),"glyph_%4x",&codepoint) != 1){
            continue;
        }
        int index = (int)codepoint - GLYPHSET_FIRST_CHAR;
        if (index < 0 || index >= GLYPHSET_NUM_CHARS){
            //A font may well carry glyphs outside printable ASCII; this set only indexes those.
            continue;
        }

        //No material list: the glyphs are exported without materials (the export script says so),
        //and an Object picks the colour its text is drawn in by material slot anyway.
        Mesh* mesh = loader.GetMeshFromNode(name.c_str(),NULL,false);
        if (!mesh){
            debug->Warn("Glyph node %s carried no mesh\n",name.c_str());
            continue;
        }
        out.glyph[index] = mesh;
        num_loaded++;
    }

    if (loader_debug){
        loader_debug->SetLevel(restore_level);
    }

    out.advance = advance;
    out.line_height = line_height;

    if (num_loaded == 0){
        debug->Err("No glyphs in %s - expected nodes named glyph_<4 hex digits>\n",filename);
        return false;
    }
    //Space legitimately has no mesh, so the count is one short of the printable set and that is
    //not worth warning about. Said out loud once because "94 glyphs" is the number to recognise.
    debug->Ok("Loaded %i glyphs from %s (advance %.4f, line height %.4f)\n",
              num_loaded,filename,advance,line_height);
    return true;
}
