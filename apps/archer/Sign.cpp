#include "Sign.h"
#include "Debug.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>

static Debugger* debug = new Debugger("Sign",DEBUG_INFO);

//The slot a child's name marks, or -1: "text_<n>", optionally followed by Blender's ".NNN".
static int SlotIndexFromName(const std::string& name){
    const char* prefix = "text_";
    size_t len = strlen(prefix);
    if (name.compare(0,len,prefix) != 0){
        return -1;
    }
    const char* digits = name.c_str() + len;
    char* end = NULL;
    long index = strtol(digits,&end,10);
    if (end == digits || (*end != '\0' && *end != '.')){
        return -1;
    }
    if (index < 0 || index >= SIGN_MAX_SLOTS){
        return -1;
    }
    return (int)index;
}

bool LoadSignModel(GLTFLoader& loader, const char* node, SignModel& out){
    out = SignModel();
    Mesh* mesh = loader.GetMeshFromNode(node,&out.materials,false);
    if (!mesh){
        debug->Err("No mesh on sign node '%s'\n",node);
        return false;
    }
    //Held by the model as well as by every sign drawing it - the same reasoning as the foliage.
    mesh->Retain();
    out.mesh = mesh;

    std::vector<std::string> children = loader.GetNodeChildNames(node);
    for (size_t i = 0; i < children.size(); i++){
        int index = SlotIndexFromName(children[i]);
        if (index < 0){
            continue;
        }
        const char* child = children[i].c_str();
        SignSlot slot;
        slot.index = index;
        slot.position = loader.GetNodePosition(child);
        slot.rotation = loader.GetNodeRotation(child);
        slot.half = loader.GetNodeScale(child);
        //A mirrored or flattened-to-nothing empty would fit the text to zero or turn it inside out.
        if (slot.half.x <= 0.0f || slot.half.y <= 0.0f || slot.half.z < 0.0f){
            debug->Warn("Sign '%s': %s has a zero or negative scale - skipped\n",node,child);
            continue;
        }
        out.slots.push_back(slot);
    }
    std::sort(out.slots.begin(),out.slots.end(),
              [](const SignSlot& a, const SignSlot& b){ return a.index < b.index; });

    if (out.slots.empty()){
        debug->Warn("Sign '%s' has no text_<n> empties parented to it - it will stand blank\n",node);
    }
    debug->Info("Sign '%s': %i text slots\n",node,(int)out.slots.size());
    return true;
}

//The slot numbered `index`, or NULL. Slots need not be contiguous: text_0 and text_2 is legal.
static const SignSlot* FindSlot(const SignModel& model, int index){
    for (size_t i = 0; i < model.slots.size(); i++){
        if (model.slots[i].index == index){
            return &model.slots[i];
        }
    }
    return NULL;
}

/*
    One string, fitted into its slot's box.

    FITTED ON THE INK, not on the font's line box. The glyph meshes are measured after baking, so
    capitals - which is what signs are written in - are centred on the capitals, rather than
    sitting high with the room a descender would need left empty underneath. The cost is that
    "rope" with its descender comes out a little smaller than "ROPE" would; a sign wanting matched
    sizes across boards would pass a size in, and the fit would become a clamp.
*/
static Object* BuildSlotText(const SignSlot& slot, const GlyphSet& glyphs, const char* text,
                             int text_material){
    TextLayout layout;
    layout.align = TEXT_ALIGN_CENTER;
    Mesh* mesh = BuildTextMesh(glyphs,text,layout,NULL);
    if (!mesh){
        return NULL;
    }
    const std::vector<vertex>& verts = mesh->GetVertices();
    vec3 lo = verts[0].pos;
    vec3 hi = verts[0].pos;
    for (size_t i = 1; i < verts.size(); i++){
        const vec3& p = verts[i].pos;
        lo.x = std::min(lo.x,p.x); lo.y = std::min(lo.y,p.y); lo.z = std::min(lo.z,p.z);
        hi.x = std::max(hi.x,p.x); hi.y = std::max(hi.y,p.y); hi.z = std::max(hi.z,p.z);
    }
    float w = hi.x - lo.x;
    float h = hi.y - lo.y;
    float d = hi.z - lo.z;
    if (w <= 0.0f || h <= 0.0f){
        return NULL;
    }
    float s = std::min(2.0f * slot.half.x / w,2.0f * slot.half.y / h);
    //Depth from the box as well, independently - relief is a different decision from size. A box
    //with no depth at all keeps the glyphs' own proportion rather than vanishing.
    float sz = (slot.half.z > 0.0f && d > 0.0f) ? 2.0f * slot.half.z / d : s;

    //The ink's centre onto the box's centre, and its back onto the box's back face.
    vec3 offset = vec3(-0.5f * (lo.x + hi.x) * s,-0.5f * (lo.y + hi.y) * s,-slot.half.z - lo.z * sz);

    Object* o = new Object();
    o->SetMesh(mesh);
    o->SetMaterialSlot(0,text_material);
    o->SetPosition(slot.position + slot.rotation * offset);
    o->SetRotation(slot.rotation);
    o->SetScale(vec3(s,s,sz));
    //Read as part of the board, not picked out of it; and too thin to be worth a shadow.
    o->SetPickability(false);
    o->SetCastsShadow(false);
    return o;
}

Object* BuildSignObject(const SignModel& model, const GlyphSet& glyphs, const char* const* texts,
                        int count, int text_material, const char* name){
    if (!model.mesh){
        return NULL;
    }
    Object* root = new Object();
    root->name = name;
    root->SetMesh(model.mesh);
    //TakeMaterialNames wants to write the list; each sign gets its own copy to hand over.
    std::vector<Material> materials = model.materials;
    root->TakeMaterialNames(materials);

    for (int i = 0; i < count; i++){
        if (!texts[i] || !texts[i][0]){
            continue;
        }
        const SignSlot* slot = FindSlot(model,i);
        if (!slot){
            debug->Warn("Sign '%s' has no text_%i for \"%s\" - not written\n",name,i,texts[i]);
            continue;
        }
        if (!glyphs.IsValid()){
            continue;
        }
        Object* text = BuildSlotText(*slot,glyphs,texts[i],text_material);
        if (!text){
            continue;
        }
        char child_name[64];
        snprintf(child_name,sizeof(child_name),"%s.text_%i",name,i);
        text->name = child_name;
        root->AttachChild(text);
    }
    return root;
}
