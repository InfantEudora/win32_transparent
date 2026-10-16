#include "SceneCapture.h"
#include "Scene.h"
#include "Object.h"
#include "Mesh.h"
#include "Material.h"
#include "Renderer.h"
#include "Shader.h"
#include "Debug.h"
#include <cmath>
#include <string.h>

static Debugger* debug = new Debugger("SceneCapture",DEBUG_ALL);

/*
    THE BYTES, version 1, little-endian as the machine writes them - a capture is read back by the build of
    this engine that wrote it or a later one, on the same kind of machine, never sent between machines:

        "SCAP" u32 version, vec3 centre, f32 radius, u32 objects, then per object
            string name; u8 kind (0 a mesh, 1 an instance set); u8 casts a shadow; i32 mesh mode;
            string shader vname, fname (empty unless MESH_MODE_SHADER); string material name x NUM_MATERIAL_SLOTS;
            u32 vertices, vertex x that (a mesh's in world space; a set's mesh as it is);
            a set: u32 instances, fmat4 x that (world space), u32 motion vec4s, vec4 x that, f32 its pad
        then (version 2) u32 markers, and per marker string kind, vec3 at, string data
    with a string as u32 length then its bytes. Version 1 - before markers - still loads.
*/
#define SCENE_CAPTURE_VERSION   2

namespace {

const char SCAP_MAGIC[4] = {'S','C','A','P'};

struct Writer{
    std::vector<uint8_t>& out;
    void Bytes(const void* p, size_t n){
        const uint8_t* b = (const uint8_t*)p;
        out.insert(out.end(),b,b + n);
    }
    template<typename T> void Put(const T& v){ Bytes(&v,sizeof(T)); }
    void String(const std::string& s){
        Put((uint32_t)s.size());
        Bytes(s.data(),s.size());
    }
};

struct Reader{
    const uint8_t* p;
    const uint8_t* end;
    bool f_bad = false;
    bool Bytes(void* to, size_t n){
        if (f_bad || (size_t)(end - p) < n){
            f_bad = true;
            return false;
        }
        memcpy(to,p,n);
        p += n;
        return true;
    }
    template<typename T> T Get(){
        T v{};
        Bytes(&v,sizeof(T));
        return v;
    }
    std::string String(){
        uint32_t n = Get<uint32_t>();
        if (f_bad || (size_t)(end - p) < n){
            f_bad = true;
            return std::string();
        }
        std::string s((const char*)p,n);
        p += n;
        return s;
    }
};

float Horizontal(const vec3& p, const vec3& centre){
    float dx = p.x - centre.x;
    float dz = p.z - centre.z;
    return std::sqrt(dx * dx + dz * dz);
}

//A direction through the 3x3 of m - a tangent's: along the object's axes, scale and all, then made unit again.
vec3 Direction(const fmat4& m, const vec3& d){
    vec3 r = vec3(m.vertex[0].x,m.vertex[0].y,m.vertex[0].z) * d.x + vec3(m.vertex[1].x,m.vertex[1].y,m.vertex[1].z) * d.y
           + vec3(m.vertex[2].x,m.vertex[2].y,m.vertex[2].z) * d.z;
    float l = r.length();
    return (l > 0.0f) ? r * (1.0f / l) : d;
}

//A normal through the inverse transpose of m's 3x3: each axis over its squared length (fmat4::inverse_transform's
//note - axes perpendicular, no shear), so a scaled object's normals still stand square to its faces.
vec3 Normal(const fmat4& m, const vec3& n){
    vec3 r;
    for (int i = 0; i < 3; i++){
        vec3 axis(m.vertex[i].x,m.vertex[i].y,m.vertex[i].z);
        float s = axis.dot(axis);
        float c = (i == 0) ? n.x : (i == 1) ? n.y : n.z;
        r = r + axis * ((s > 0.0f) ? c / s : 0.0f);
    }
    float l = r.length();
    return (l > 0.0f) ? r * (1.0f / l) : n;
}

//The name of what slot `s` holds: the name it was given, else the name of the material its index points at.
std::string SlotName(Object* o, Renderer& renderer, int s){
    const std::string& named = o->GetMaterialNames()[s];
    if (!named.empty()){
        return named;
    }
    int index = o->GetMaterialSlots()[s];
    Material* m = (index >= 0) ? renderer.GetMaterial(index) : NULL;
    return m ? m->name : std::string();
}

}

bool SceneCaptureWrite(Scene& scene, Renderer& renderer, const SceneCaptureOptions& options, std::vector<uint8_t>& out,
                       SceneCaptureReport* report){
    SceneCaptureReport r;
    out.clear();
    Writer w{out};
    w.Bytes(SCAP_MAGIC,4);
    w.Put((uint32_t)SCENE_CAPTURE_VERSION);
    w.Put(options.centre);
    w.Put(options.radius);
    size_t count_at = out.size();
    w.Put((uint32_t)0);
    uint32_t count = 0;
    std::vector<vertex> kept;
    for (Object* o : scene.objects){
        if (!o || !o->IsVisible() || (options.keep && !options.keep(o))){
            continue;
        }
        Mesh* mesh = o->GetMesh();
        if (!mesh){
            continue;
        }
        if (mesh->mesh_mode != MESH_MODE_NORMAL && mesh->mesh_mode != MESH_MODE_SHADER){
            r.skipped++;
            continue;
        }
        const std::vector<vertex>& verts = mesh->GetVertices();
        const fmat4& world = o->GetWorldTransformScaleMatrix();
        std::vector<fmat4> instances;
        std::vector<vec4> motion;
        kept.clear();
        if (o->IsInstanceSet()){
            const std::vector<fmat4>& all = o->GetInstances();
            const std::vector<vec4>& all_motion = o->GetInstanceMotion();
            for (size_t i = 0; i < all.size(); i++){
                fmat4 t = world * all[i];
                if (Horizontal(vec3(t.vertex[3].x,t.vertex[3].y,t.vertex[3].z),options.centre) > options.radius){
                    continue;
                }
                instances.push_back(t);
                for (int k = 0; k < INSTANCE_MOTION_VEC4S && all_motion.size() == all.size() * INSTANCE_MOTION_VEC4S; k++){
                    motion.push_back(all_motion[i * INSTANCE_MOTION_VEC4S + k]);
                }
            }
            if (instances.empty()){
                r.skipped++;
                continue;
            }
            kept = verts;
        }else{
            //Whole triangles, by where their middle is: draws are non-indexed (Mesh.h), so every three is one.
            for (size_t t = 0; t + 2 < verts.size(); t += 3){
                vertex v[3];
                vec3 middle;
                for (int k = 0; k < 3; k++){
                    v[k] = verts[t + k];
                    v[k].pos = world * v[k].pos;
                    v[k].normal = Normal(world,v[k].normal);
                    v[k].tangent = Direction(world,v[k].tangent);
                    middle = middle + v[k].pos * (1.0f / 3.0f);
                }
                if (Horizontal(middle,options.centre) <= options.radius){
                    kept.insert(kept.end(),v,v + 3);
                }
            }
            if (kept.empty()){
                r.skipped++;
                continue;
            }
        }
        w.String(o->name);
        w.Put((uint8_t)(o->IsInstanceSet() ? 1 : 0));
        w.Put((uint8_t)(o->CastsShadow() ? 1 : 0));
        w.Put((int32_t)mesh->mesh_mode);
        Shader* shader = (mesh->mesh_mode == MESH_MODE_SHADER) ? renderer.GetCustomShader(mesh->custom_shader_index) : NULL;
        w.String(shader ? shader->vname : std::string());
        w.String(shader ? shader->fname : std::string());
        for (int s = 0; s < NUM_MATERIAL_SLOTS; s++){
            w.String(SlotName(o,renderer,s));
        }
        w.Put((uint32_t)kept.size());
        w.Bytes(kept.data(),kept.size() * sizeof(vertex));
        if (o->IsInstanceSet()){
            w.Put((uint32_t)instances.size());
            w.Bytes(instances.data(),instances.size() * sizeof(fmat4));
            w.Put((uint32_t)motion.size());
            w.Bytes(motion.data(),motion.size() * sizeof(vec4));
            w.Put(o->GetInstanceMotionPad());
            r.instances += (int)instances.size();
        }else{
            r.triangles += (int)kept.size() / 3;
        }
        count++;
    }
    memcpy(out.data() + count_at,&count,sizeof(count));
    w.Put((uint32_t)options.markers.size());
    for (const SceneCaptureMarker& m : options.markers){
        w.String(m.kind);
        w.Put(m.at);
        w.String(m.data);
    }
    r.objects = (int)count;
    r.markers = (int)options.markers.size();
    r.bytes = out.size();
    if (report){
        *report = r;
    }
    debug->Info("Captured %i objects round (%.1f, %.1f) r %.1f: %i triangles, %i instances, %i skipped, %i markers, %zu bytes\n",
                r.objects,options.centre.x,options.centre.z,options.radius,r.triangles,r.instances,r.skipped,r.markers,r.bytes);
    return true;
}

bool SceneCaptureLoad(const uint8_t* data, size_t size, Scene& into, Renderer& renderer, std::vector<Object*>* made,
                      SceneCaptureInfo* info, std::string& error){
    Reader in{data,data + size};
    char magic[4] = {};
    in.Bytes(magic,4);
    uint32_t version = in.Get<uint32_t>();
    if (in.f_bad || memcmp(magic,SCAP_MAGIC,4) != 0 || version < 1 || version > SCENE_CAPTURE_VERSION){
        error = "not a scene capture, or another version of one";
        return false;
    }
    SceneCaptureInfo head;
    head.centre = in.Get<vec3>();
    head.radius = in.Get<float>();
    uint32_t count = in.Get<uint32_t>();
    for (uint32_t c = 0; c < count && !in.f_bad; c++){
        std::string name = in.String();
        uint8_t kind = in.Get<uint8_t>();
        uint8_t shadow = in.Get<uint8_t>();
        int32_t mode = in.Get<int32_t>();
        std::string vname = in.String();
        std::string fname = in.String();
        std::string slots[NUM_MATERIAL_SLOTS];
        for (int s = 0; s < NUM_MATERIAL_SLOTS; s++){
            slots[s] = in.String();
        }
        uint32_t nv = in.Get<uint32_t>();
        if (in.f_bad || (size_t)(in.end - in.p) < (size_t)nv * sizeof(vertex)){
            in.f_bad = true;
            break;
        }
        std::vector<vertex> verts(nv);
        in.Bytes(verts.data(),(size_t)nv * sizeof(vertex));
        std::vector<fmat4> instances;
        std::vector<vec4> motion;
        float pad = 0.0f;
        if (kind == 1){
            uint32_t ni = in.Get<uint32_t>();
            if (in.f_bad || (size_t)(in.end - in.p) < (size_t)ni * sizeof(fmat4)){
                in.f_bad = true;
                break;
            }
            instances.resize(ni);
            in.Bytes(instances.data(),(size_t)ni * sizeof(fmat4));
            uint32_t nm = in.Get<uint32_t>();
            if (in.f_bad || (size_t)(in.end - in.p) < (size_t)nm * sizeof(vec4)){
                in.f_bad = true;
                break;
            }
            motion.resize(nm);
            in.Bytes(motion.data(),(size_t)nm * sizeof(vec4));
            pad = in.Get<float>();
        }
        if (in.f_bad){
            break;
        }
        Mesh* mesh = new Mesh();
        mesh->SetMeshData(verts.data(),(int)verts.size());
        if (mode == MESH_MODE_SHADER){
            //The custom shader by its two files, among those the app has added - not by its index, which is
            //only the order they were added in.
            int found = -1;
            for (size_t i = 0; i < renderer.custom_shaders.size() && found < 0; i++){
                Shader* sh = renderer.custom_shaders[i];
                found = (sh && sh->vname == vname && sh->fname == fname) ? (int)i : -1;
            }
            if (found < 0){
                debug->Warn("'%s': no custom shader %s + %s - drawn with the default\n",name.c_str(),vname.c_str(),fname.c_str());
            }else{
                mesh->mesh_mode = MESH_MODE_SHADER;
                mesh->custom_shader_index = found;
            }
        }
        Object* o = new Object();
        o->name = name;
        o->SetMesh(mesh);
        for (int s = 0; s < NUM_MATERIAL_SLOTS; s++){
            if (!slots[s].empty()){
                o->SetMaterialName(s,slots[s]);
            }
        }
        o->SetCastsShadow(shadow != 0);
        o->SetPickability(false);
        o->SetVisualOnly(true);
        if (kind == 1){
            size_t n = instances.size();
            o->SetInstances(std::move(instances));
            if (!motion.empty() && motion.size() == n * INSTANCE_MOTION_VEC4S){
                o->SetInstanceMotion(std::move(motion),pad);
            }
        }
        into.AddObject(o);
        if (made){
            made->push_back(o);
        }
    }
    if (version >= 2 && !in.f_bad){
        uint32_t nm = in.Get<uint32_t>();
        for (uint32_t i = 0; i < nm && !in.f_bad; i++){
            SceneCaptureMarker m;
            m.kind = in.String();
            m.at = in.Get<vec3>();
            m.data = in.String();
            head.markers.push_back(m);
        }
    }
    if (in.f_bad){
        error = "the capture ends early - cut short or damaged";
        return false;
    }
    if (info){
        *info = head;
    }
    debug->Info("Loaded a capture of %u objects and %zu markers round (%.1f, %.1f) r %.1f into '%s'\n",count,
                head.markers.size(),head.centre.x,head.centre.z,head.radius,into.name.c_str());
    return true;
}
