"""Re-apply this repo's adaptations onto a verbatim copy of the port's sprite_packer.

The repo's own merge method (docs/engine_backlog.md, "Reference: merging the Android port"):
copy the upstream file WHOLESALE, then re-apply each local adaptation from a script that
ASSERTS ON ITS ANCHOR - so an upstream change that invalidates an adaptation fails loudly
instead of the adaptation silently vanishing. Re-run this after re-copying from the port.
"""
import io

BASE = 'tools/spritepacker/'


def edit(name, pairs):
    p = BASE + name
    s = io.open(p, encoding='utf-8').read()
    for old, new in pairs:
        assert s.count(old) == 1, "%s: no unique anchor for %r" % (name, old[:70])
        s = s.replace(old, new)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)
    print("  adapted", name)


# ---------------------------------------------------------------- the header
edit('SpritePackerApplication.h', [
    # The port's Application carries the scene; ours hands one out from CreateNewScene.
    ('#include "Application.h"\n#include "SpritePackerScene.h"\n#include "SpriteSheet.h"\n#include "Texture.h"',
     '#include "Application.h"\n#include "SpriteSheet.h"\n#include "Texture.h"'),

    # The port's verification default points into the port's own checkout. Blank here, so the
    # tool opens with nothing loaded and Browse... is the first thing you reach for.
    ('char folder_path_buf[512] = "C:\\\\code\\\\android\\\\hello_world\\\\assets"; // TEMP: verification default, reverted after testing',
     'char folder_path_buf[512] = "";'),

    ('''protected:
    void SetupScene() override;

    // Adds the "Source Folder" tab (folder picker, file listing, sprite
    // slider/preview/UV editor) to the ImGui debug panel's tab bar -- see
    // core/Application.h's comment on this hook.
    void BuildAppTabs() override;

    // Draws the "drop a file onto the window" import-confirmation modal --
    // see core/Application.h's comment on this hook for why this needs to
    // be its own top-level call rather than living inside BuildAppTabs().
    void RenderUI() override;

private:
    // Owns the actual Scene subclass instance -- Application::main_scene is
    // pointed at this in the constructor below, same pattern as
    // sprite_demo/SpriteApplication.h's sprite_scene member.
    SpritePackerScene scene_impl;
''',
     '''protected:
    /*
        ADAPTED FROM THE PORT'S SetupScene(). This engine has no SetupScene hook - Init() is
        where an app builds its renderer, its default shader and its scene, and it is equally
        "the first point a GL context and a window actually exist", which is what the port's
        comment on SetupScene was really claiming. See any app's Init for the prologue.
    */
    void Init(void) override;

#ifdef USE_IMGUI
    /*
        ADAPTED FROM THE PORT'S BuildAppTabs() + RenderUI(), which are two hooks this engine
        does not have: apps here override DrawImGuiUI and draw their own windows. So the port's
        "Source Folder" TAB becomes this tool's own WINDOW, and the dropped-file modal - which
        upstream had to keep out of BuildAppTabs because a modal cannot live inside a tab item -
        simply follows it in the same function. Nothing about either body changed.
    */
    void DrawImGuiUI(void) override;
#endif

private:
'''),
])

# ---------------------------------------------------------------- the source
edit('SpritePackerApplication.cpp', [
    ('#include "imgui.h"',
     '''#include "imgui.h"

#include "Renderer.h"
#include "Shader.h"

/*
    ADDED: this engine has no global `debug`. Every app and tool here declares its own, named
    after itself, so a line in stderr says which component wrote it - see any main.cpp.
*/
static Debugger* debug = new Debugger("SpritePacker",DEBUG_ALL);

/*
    ADDED: the port has Texture::InvertRGB in its core; this tree does not.

    Kept HERE rather than added to core/Texture, because it is a button in one tool and core
    gains nothing by carrying it. Every member it touches is public on this side too, so it
    works as a free function unchanged. It is a fair upstream candidate if anything else ever
    wants it - see docs/engine_backlog.md items 68-77 for that direction of traffic.
*/
static void InvertTextureRGB(Texture* texture){
    if (!texture || !texture->img_data || texture->img_data_sz == 0){
        debug->Warn("InvertTextureRGB: no decoded image data to invert\\n");
        return;
    }
    int channels = (texture->image_format == GL_RGBA) ? 4 : 3;
    size_t pixel_count = texture->img_data_sz / channels;
    for (size_t i = 0; i < pixel_count; i++){
        uint8_t* pixel = texture->img_data + (i * channels);
        pixel[0] = 255 - pixel[0];
        pixel[1] = 255 - pixel[1];
        pixel[2] = 255 - pixel[2];
        //Alpha, where there is one, is deliberately left alone.
    }
    texture->UploadTexture(texture->image_format);
}

/*
    Shifts sprite's pixel rect by (dx,dy), clamped so it stays inside atlas, then re-derives
    the UVs. HOISTED HERE from between the port's two UI functions - it sat between RenderUI
    and BuildAppTabs, and those are one function on this side, so leaving it where it was put
    a function definition inside another one.
*/
static void NudgeSpritePixelRect(Sprite* sprite, Texture* atlas, int dx, int dy){
    int max_x = std::max(0, atlas->width - sprite->width);
    int max_y = std::max(0, atlas->height - sprite->height);
    sprite->x = std::clamp(sprite->x + dx, 0, max_x);
    sprite->y = std::clamp(sprite->y + dy, 0, max_y);
    sprite->CalculateUV();
}'''),

    # Its original definition site, now that it has been hoisted above the merged UI function.
    ('''// Shifts sprite's pixel rect by (dx,dy) pixels, clamped so it stays fully
// inside atlas (never lets x/y go negative or push x+width/y+height past
// the image edge), then re-derives uv0/uv1 to match. Used by the "Nudge"
// buttons below.
static void NudgeSpritePixelRect(Sprite* sprite, Texture* atlas, int dx, int dy){
    int max_x = std::max(0, atlas->width - sprite->width);
    int max_y = std::max(0, atlas->height - sprite->height);
    sprite->x = std::clamp(sprite->x + dx, 0, max_x);
    sprite->y = std::clamp(sprite->y + dy, 0, max_y);
    sprite->CalculateUV();
}

''', ''),

    #This tree exposes the handle as a public member rather than through a getter.
    ('main_window->GetHWND()', 'main_window->hWnd'),

    ('                        atlas->InvertRGB();', '                        InvertTextureRGB(atlas);'),

    # Constructor: this engine has no shader-name members and no app-owned Scene.
    ('''    app_name = "Sprite Packer";
    // Desktop GL's core profile rejects cube.vert/cube.frag's "#version 310
    // es" outright -- cube_desktop.vert/frag (shared_assets/) are a
    // byte-for-byte-compatible-uniform-layout translation of the same two
    // files for a desktop GL context. See those files' header comments.
    shader_vert_name = "cube_desktop.vert";
    shader_frag_name = "cube_desktop.frag";
    main_scene = &scene_impl;
}''',
     '''    app_name = "Sprite Packer";
    /*
        THE PORT SET shader_vert_name/shader_frag_name HERE and pointed main_scene at its own
        Scene subclass. Neither exists in this engine: the shader is constructed by name in
        Init() like every app here does, and the scene comes from CreateNewScene. The port's
        cube_desktop.vert/frag were its answer to desktop GL rejecting "#version 310 es", which
        is not a problem this tree has - shaders/default.* are already desktop shaders.
    */
}'''),

    ('''// TODO(sprite_packer): nothing built yet -- populate once the real sprite-
// packing UI/scene content is plugged in.
void SpritePackerApplication::SetupScene() {
    // main_window doesn't exist yet in the constructor -- it's only created
    // later, in Application::Start() -- so this has to happen here instead:
    // SetupScene() is the base class's documented "first point a GL context/
    // window actually exists" hook.
    main_window->SetOnFileDropped([this](std::string filename){''',
     '''/*
    The graphics prologue, then the tool's own setup. RENDER THREAD, once.

    The first four statements are this engine's standard app opening - renderer, pipeline,
    default shader, scene - and are copied from apps/ui, the smallest app here. The port had
    them in its Application base class instead, which is why its SetupScene could be this
    function's last three lines alone. (That base-class prologue is backlog item 89 in this
    tree: ours is dead code that every app bypasses.)
*/
void SpritePackerApplication::Init(void) {
    renderer = new Renderer(main_window->width,main_window->height);
    if (!renderer->Init(PIPELINE_DEFERRED)){
        debug->Fatal("Failed to Initialise Rendering Pipeline\\n");
    }
    default_shader = new Shader("shaders/default.vert","shaders/default.frag");
    main_window->Resize(1280,860);
    main_scene = CreateNewScene("Sprite Packer Scene");

    //main_window does not exist in the constructor - it is created in Application::Start() -
    //so the drop handler has to be installed here.
    main_window->SetOnFileDropped([this](std::string filename){'''),

    # The two UI entry points become one.
    ('void SpritePackerApplication::RenderUI(){',
     '''#ifdef USE_IMGUI
/*
    The tool's whole UI. RENDER THREAD, physics_mutex held - see the threading note in
    core/Application.h.

    UPSTREAM THIS WAS TWO FUNCTIONS, RenderUI() and BuildAppTabs(), because the port's engine
    calls both and a modal cannot be opened from inside an ImGui tab item. Here there is one
    hook, so the modal is drawn first and the panel follows it; the bodies are otherwise the
    port's, unchanged.
*/
void SpritePackerApplication::DrawImGuiUI(void){'''),

    ('void SpritePackerApplication::BuildAppTabs(){\n    if (ImGui::BeginTabItem("Source Folder")){',
     '''    //--- the panel, which upstream is a tab in the engine's own debug window --------------
    ImGui::Begin("Sprite Packer");
    {'''),
])

# The tail of the old BuildAppTabs closes a tab item; here it closes a window.
p = BASE + 'SpritePackerApplication.cpp'
s = io.open(p, encoding='utf-8').read()
old_tail = '''
        ImGui::EndTabItem();
    }
}
'''
new_tail = '''
    }
    ImGui::End();
}
#endif //USE_IMGUI
'''
assert s.count(old_tail) == 1, "no unique anchor for the BuildAppTabs tail"
s = s.replace(old_tail, new_tail)

# RenderUI's own closing brace now runs straight into the panel rather than ending a function.
old_join = '''        ImGui::EndPopup();
    }
}
'''
new_join = '''        ImGui::EndPopup();
    }

'''
assert s.count(old_join) == 1, "no unique anchor for the RenderUI/BuildAppTabs join"
s = s.replace(old_join, new_join)
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print("  adapted SpritePackerApplication.cpp (tail)")

# ---------------------------------------------------------------- main.cpp
edit('main.cpp', [
    ('''#include "SpritePackerApplication.h"
#include <windows.h>
''',
     '''#include <windows.h>

//NVidia Optimus: picked up by the driver to choose the discrete GPU on a laptop. Every main.cpp
//in this tree carries it - see apps/bomber/main.cpp.
extern "C" { __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001; }

#include "SpritePackerApplication.h"
'''),

    ('#include "SpritePackerApplication.h"',
     '#include "File.h"\n\n#include "SpritePackerApplication.h"'),

    ('''    static SpritePackerApplication application;
    // No Init() call any more: the Win32 bootstrap it used to do moved into
    // Application's constructor. `Init` is now the engine's virtual app hook.
    application.Start();
    return application.Shutdown();''',
     '''    /*
        ADDED HERE, not in the port: this engine resolves every asset name against roots the app
        declares, and the tool needs shaders/default.* out of shared_assets. The port's
        Application found its own; ours does not - see core/File.h.

        ONE ROOT, because this tool has no assets of its own. The images it packs are chosen at
        run time through the folder picker and read by absolute path, so they never go through
        the search path at all.
    */
    AddAssetSearchRootFromExe("../../../shared_assets");    //default shaders and fonts

    static SpritePackerApplication application;
    //Start() creates the threads and calls the app's Init(); Exit() is this engine's spelling
    //of the port's Shutdown().
    application.Start();
    return application.Exit();'''),
])

print("adaptation complete")
