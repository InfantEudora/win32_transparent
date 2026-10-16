#ifndef _SCENECAPTURE_H_
#define _SCENECAPTURE_H_

#include <functional>
#include <string>
#include <vector>
#include <stdint.h>
#include "type_vec2.h"
#include "type_vec3.h"

class Scene;
class Object;
class Renderer;

/*
    A SCENE CAPTURE: what one scene draws within a radius of a point, written to bytes, and loaded from them
    into another scene as plain objects - a DIORAMA. apps/chasm's title shows one (apps/chasm/docs/
    demo_plan.md): a piece of a live world, frozen, where that world is not loaded. Opt-in: nothing in core
    calls it.

    WHAT COMES ALONG, for each visible object the app keeps (`keep`):
      - a NORMAL or a custom-SHADER mesh: the triangles whose middle lies within the radius (x and z: a cliff
        going down stays whole), each vertex baked into world space. A custom shader is named by its two
        files (Shader::vname, fname), found again among the renderer's custom shaders when loaded;
      - its material slots BY NAME, so they resolve against the renderer's list however that list was built
        (Object.h, "MATERIALS") - a vertex's matid is a slot, and is copied as it is;
      - an INSTANCE SET: the instances standing within the radius, each baked into world space, with their
        motion if they move (Object::SetInstanceMotion) - smoke goes on rising;
      - whether it casts a shadow.
    WHAT DOES NOT: skinned and line meshes, lights, cameras, parent links (every object stands alone at the
    origin, its world baked in), pickability, and anything of the app's rules.

    MARKERS: points the app names in the capture, with its own data, handed back as they were written - for
    what it brings to life when it loads one, which a frozen mesh cannot be. Chasm's: a chimney's top, smoke
    to rise from; a person and what he was doing, to go on doing it.

    Bytes rather than a file, so the app says where they go - a baked asset for a ship build. Render thread
    for both: a capture reads meshes the render thread uploads, and a load makes GL buffers.
*/
struct SceneCaptureMarker{
    std::string kind;
    vec3 at;
    std::string data;                           //the app's own, opaque here (Chasm writes JSON)
};

struct SceneCaptureOptions{
    vec3 centre;                                //what the capture is round; its y is kept for the loader's camera
    float radius = 40.0f;                       //horizontal: x and z
    std::function<bool(Object*)> keep;          //the app's say over which objects come along; empty keeps every visible one
    std::vector<SceneCaptureMarker> markers;    //written as they are: the app chose them
};

struct SceneCaptureReport{
    int objects = 0;
    int triangles = 0;
    int instances = 0;
    int skipped = 0;                            //visible, but skinned, a line mesh, or nothing within the radius
    int markers = 0;
    size_t bytes = 0;
};

//What a loaded capture says about itself: where it was taken round, for the loader to frame it, and its markers.
struct SceneCaptureInfo{
    vec3 centre;
    float radius = 0.0f;
    std::vector<SceneCaptureMarker> markers;
};

bool SceneCaptureWrite(Scene& scene, Renderer& renderer, const SceneCaptureOptions& options, std::vector<uint8_t>& out,
                       SceneCaptureReport* report = NULL);
//Its objects made and added to `into`, at the origin; `made` lists them for the caller to hide or remove.
bool SceneCaptureLoad(const uint8_t* data, size_t size, Scene& into, Renderer& renderer, std::vector<Object*>* made,
                      SceneCaptureInfo* info, std::string& error);

#endif
