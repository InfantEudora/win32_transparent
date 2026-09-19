#include "SpritePackerApplication.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>

#include "imgui.h"

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
        debug->Warn("InvertTextureRGB: no decoded image data to invert\n");
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
}

#include <windows.h>
#include <shobjidl.h>

namespace fs = std::filesystem;

SpritePackerApplication::SpritePackerApplication() {
    app_name = "Sprite Packer";
    /*
        THE PORT SET shader_vert_name/shader_frag_name HERE and pointed main_scene at its own
        Scene subclass. Neither exists in this engine: the shader is constructed by name in
        Init() like every app here does, and the scene comes from CreateNewScene. The port's
        cube_desktop.vert/frag were its answer to desktop GL rejecting "#version 310 es", which
        is not a problem this tree has - shaders/default.* are already desktop shaders.
    */
}

/*
    The graphics prologue, then the tool's own setup. RENDER THREAD, once.

    The first four statements are this engine's standard app opening - renderer, pipeline,
    default shader, scene - and are copied from apps/ui, the smallest app here. The port had
    them in its Application base class instead, which is why its SetupScene could be this
    function's last three lines alone. (That base-class prologue is backlog item 89 in this
    tree: ours is dead code that every app bypasses.)
*/
void SpritePackerApplication::Init(void) {
    renderer = new Renderer(main_window->width,main_window->height);
    if (!renderer->Init("shaders/default.vert","shaders/deferred.frag",PIPELINE_DEFERRED)){
        debug->Fatal("Failed to Initialise Rendering Pipeline\n");
    }
    default_shader = new Shader("shaders/default.vert","shaders/default.frag");
    main_window->Resize(1280,860);
    main_scene = CreateNewScene("Sprite Packer Scene");

    //main_window does not exist in the constructor - it is created in Application::Start() -
    //so the drop handler has to be installed here.
    main_window->SetOnFileDropped([this](std::string filename){
        //Create a modal window in the UI:
        f_filemodal = true;
        filemodal_filename = filename;
    });

    RefreshFileList(); // TEMP: verification default, reverted after testing
}

static bool HasPngExtension(const fs::path& path){
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return (char)std::tolower(c); });
    return ext == ".png";
}

void SpritePackerApplication::RefreshFileList(){
    file_list.clear();
    file_list_error.clear();

    if (folder_path_buf[0] != '\0'){
        std::error_code ec;
        fs::path dir(folder_path_buf);
        if (!fs::is_directory(dir, ec)){
            file_list_error = "Not a folder (or doesn't exist)";
        } else {
            fs::directory_iterator it(dir, ec);
            if (ec){
                file_list_error = "Could not open folder: " + ec.message();
            } else {
                // Not a range-based for: that would call directory_iterator's
                // throwing operator++() on every step, and one unreadable
                // entry (a permissions error, a broken symlink) would throw
                // std::filesystem_error and take the whole app down with it.
                // it.increment(ec) is the non-throwing equivalent.
                for (; it != fs::end(it); it.increment(ec)){
                    if (ec){
                        break;
                    }
                    if (it->is_regular_file(ec) && HasPngExtension(it->path())){
                        file_list.push_back(it->path().filename().string());
                    }
                }
                std::sort(file_list.begin(), file_list.end());
            }
        }
    }

    LoadImages();
}

// IFileOpenDialog with FOS_PICKFOLDERS -- the modern replacement for the
// legacy SHBrowseForFolder. STA COM is initialized/uninitialized around the
// call rather than once at startup, since this is the only COM consumer in
// the whole tool and it's only ever needed while this dialog is on screen.
static bool ShowFolderPickerDialog(HWND owner, std::string& out_path){
    bool picked = false;

    HRESULT hr_init = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    // RPC_E_CHANGED_MODE means this thread's COM apartment was already
    // initialized (in a different mode) by someone else -- still usable,
    // just don't CoUninitialize() a context we didn't create.
    bool need_uninit = SUCCEEDED(hr_init);

    IFileOpenDialog* dialog = NULL;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, (void**)&dialog);
    if (SUCCEEDED(hr)){
        DWORD flags = 0;
        dialog->GetOptions(&flags);
        dialog->SetOptions(flags | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);

        if (SUCCEEDED(dialog->Show(owner))){
            IShellItem* item = NULL;
            if (SUCCEEDED(dialog->GetResult(&item))){
                PWSTR wpath = NULL;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &wpath))){
                    int len = WideCharToMultiByte(CP_UTF8, 0, wpath, -1, NULL, 0, NULL, NULL);
                    if (len > 0){
                        out_path.resize(len - 1); // len includes the null terminator
                        WideCharToMultiByte(CP_UTF8, 0, wpath, -1, out_path.data(), len, NULL, NULL);
                        picked = true;
                    }
                    CoTaskMemFree(wpath);
                }
                item->Release();
            }
        }
        dialog->Release();
    }

    if (need_uninit){
        CoUninitialize();
    }
    return picked;
}

void SpritePackerApplication::BrowseForFolder(){
    std::string picked_path;
    if (!ShowFolderPickerDialog(main_window->hWnd, picked_path)){
        return; // cancelled or failed -- leave folder_path_buf as-is
    }

    strncpy(folder_path_buf, picked_path.c_str(), sizeof(folder_path_buf) - 1);
    folder_path_buf[sizeof(folder_path_buf) - 1] = '\0';
    RefreshFileList();
}

void SpritePackerApplication::LoadImages(){
    sprite_sheet.Clear();
    loaded_textures.clear(); // destroys each Texture -- now frees its GPU handle too, see Texture::~Texture()
    slider_index = 0;
    viewed_index = -1;

    for (const std::string& filename : file_list){
        std::string full_path = std::string(folder_path_buf) + "\\" + filename;

        std::unique_ptr<Texture> tex(new Texture());
        tex->LoadFromFile(full_path.c_str());
        if (tex->IsEmpty()){
            continue; // Texture::LoadFromFile()/LoadFromMemory() already logged why
        }

        sprite_sheet.AddSpriteFromWholeTexture(tex.get(), filename.c_str());
        loaded_textures.push_back(std::move(tex));
    }

    if (!sprite_sheet.sprites.empty()){
        viewed_index = 0;
    }
}

void SpritePackerApplication::ImportDroppedFile(const std::string& path){
    std::unique_ptr<Texture> tex(new Texture());
    tex->LoadFromFile(path.c_str());
    if (tex->IsEmpty()){
        return; // Texture::LoadFromFile()/LoadFromMemory() already logged why
    }

    // Named by its own filename, matching every other sprite (see
    // LoadImages()) -- even though a dropped file isn't necessarily under
    // folder_path_buf at all.
    std::string name = fs::path(path).filename().string();
    sprite_sheet.AddSpriteFromWholeTexture(tex.get(), name.c_str());
    loaded_textures.push_back(std::move(tex));

    viewed_index = (int)sprite_sheet.sprites.size() - 1;
    slider_index = viewed_index;
}

#ifdef USE_IMGUI
/*
    The tool's whole UI. RENDER THREAD, physics_mutex held - see the threading note in
    core/Application.h.

    UPSTREAM THIS WAS TWO FUNCTIONS, RenderUI() and BuildAppTabs(), because the port's engine
    calls both and a modal cannot be opened from inside an ImGui tab item. Here there is one
    hook, so the modal is drawn first and the panel follows it; the bodies are otherwise the
    port's, unchanged.
*/
void SpritePackerApplication::DrawImGuiUI(void){
    // OpenPopup()/BeginPopupModal() identify a popup by this exact string
    // (ImGui's popups are matched by ID, not by call site) -- they have to
    // match precisely, or OpenPopup() opens a popup nothing ever displays.
    static const char* kImportPopupId = "Import dropped file?";

    if (f_filemodal){
        ImGui::OpenPopup(kImportPopupId);
        f_filemodal = false; // OpenPopup() only needs to fire once, not every frame the flag stays set
    }

    // Always center this window when appearing
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(kImportPopupId, NULL, ImGuiWindowFlags_AlwaysAutoResize)){
        ImGui::Text("Attempt to import [%s]?",filemodal_filename.c_str());
        ImGui::Separator();
        if (ImGui::Button("Yes, go ahead.", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
            ImportDroppedFile(filemodal_filename);
        }
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (ImGui::Button("No, never mind", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }


    //--- the panel, which upstream is a tab in the engine's own debug window --------------
    ImGui::Begin("Sprite Packer");
    {
        ImGui::TextUnformatted("Folder to pack sprites from:");

        bool path_entered = ImGui::InputText("##folder_path", folder_path_buf, sizeof(folder_path_buf), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if (ImGui::Button("Browse...")){
            BrowseForFolder();
        }
        ImGui::SameLine();
        bool refresh_clicked = ImGui::Button("Refresh");
        if (path_entered || refresh_clicked){
            RefreshFileList();
        }

        ImGui::Separator();

        if (!file_list_error.empty()){
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", file_list_error.c_str());
        } else {
            ImGui::Text("%d PNG file(s), %d loaded", (int)file_list.size(), (int)sprite_sheet.sprites.size());

            ImGui::BeginChild("SourceFolderFileList", ImVec2(0, 150), true);
            for (const std::string& name : file_list){
                bool is_viewed = (viewed_index >= 0 && (size_t)viewed_index < sprite_sheet.sprites.size()
                    && sprite_sheet.sprites[viewed_index].name == name);
                if (ImGui::Selectable(name.c_str(), is_viewed)){
                    Sprite* clicked = sprite_sheet.GetSprite(name.c_str());
                    if (clicked){
                        int idx = (int)(clicked - sprite_sheet.sprites.data());
                        slider_index = idx;
                        viewed_index = idx;
                    }
                }
            }
            ImGui::EndChild();

            if (!sprite_sheet.sprites.empty()){
                ImGui::Separator();

                int max_index = (int)sprite_sheet.sprites.size() - 1;
                Sprite* slider_sprite = sprite_sheet.GetSprite(slider_index);
                ImGui::SliderInt("Sprite", &slider_index, 0, max_index, slider_sprite ? slider_sprite->name.c_str() : "");
                // Only commits the new selection (and so what the preview/UV
                // editor below show) once the slider is released -- not on
                // every in-between value while still dragging.
                if (ImGui::IsItemDeactivatedAfterEdit()){
                    viewed_index = slider_index;
                }

                Sprite* viewed_sprite = (viewed_index >= 0) ? sprite_sheet.GetSprite(viewed_index) : NULL;
                if (viewed_sprite && viewed_sprite->atlas){
                    Texture* atlas = viewed_sprite->atlas;
                    ImGui::Separator();
                    ImGui::Text("Viewing: %s (%d x %d)", viewed_sprite->name.c_str(), atlas->width, atlas->height);

                    // Fit the preview within a max box, keeping aspect ratio --
                    // never upscales a smaller-than-the-box source image.
                    const float max_preview_px = 256.0f;
                    float scale = 1.0f;
                    float largest_dim = (float)std::max(atlas->width, atlas->height);
                    if (largest_dim > max_preview_px){
                        scale = max_preview_px / largest_dim;
                    }
                    ImVec2 preview_size((float)atlas->width * scale, (float)atlas->height * scale);
                    ImTextureID tex_id = (ImTextureID)(intptr_t)atlas->texture_id;

                    ImGui::Image(tex_id, preview_size);

                    ImGui::TextUnformatted("UV rect (drag to edit):");
                    bool uv_changed = false;
                    uv_changed |= ImGui::DragFloat2("UV0", &viewed_sprite->uv0.x, 0.001f, 0.0f, 1.0f);
                    uv_changed |= ImGui::DragFloat2("UV1", &viewed_sprite->uv1.x, 0.001f, 0.0f, 1.0f);
                    if (uv_changed){
                        viewed_sprite->CalculatePixelRect();
                    }

                    // Same edit, in pixel coordinates -- Pixel0/Pixel1 are
                    // the same two corners as UV0/UV1 above, just expressed
                    // in atlas pixels instead of normalized 0..1 UV.
                    ImGui::TextUnformatted("Pixel rect (same thing, in pixels):");
                    int px0[2] = { viewed_sprite->x, viewed_sprite->y };
                    int px1[2] = { viewed_sprite->x + viewed_sprite->width, viewed_sprite->y + viewed_sprite->height };
                    bool pixel_changed = false;
                    pixel_changed |= ImGui::DragInt2("Pixel0", px0, 1.0f);
                    pixel_changed |= ImGui::DragInt2("Pixel1", px1, 1.0f);
                    if (pixel_changed){
                        px0[0] = std::clamp(px0[0], 0, atlas->width);
                        px0[1] = std::clamp(px0[1], 0, atlas->height);
                        px1[0] = std::clamp(px1[0], px0[0], atlas->width);
                        px1[1] = std::clamp(px1[1], px0[1], atlas->height);

                        viewed_sprite->x = px0[0];
                        viewed_sprite->y = px0[1];
                        viewed_sprite->width = px1[0] - px0[0];
                        viewed_sprite->height = px1[1] - px0[1];
                        viewed_sprite->CalculateUV();
                    }

                    ImGui::Text("Mapped size: %d x %d px", viewed_sprite->width, viewed_sprite->height);

                    ImGui::TextUnformatted("Nudge (1px):");
                    if (ImGui::Button("Up")){ NudgeSpritePixelRect(viewed_sprite, atlas, 0, -1); }
                    ImGui::SameLine();
                    if (ImGui::Button("Down")){ NudgeSpritePixelRect(viewed_sprite, atlas, 0, 1); }
                    ImGui::SameLine();
                    if (ImGui::Button("Left")){ NudgeSpritePixelRect(viewed_sprite, atlas, -1, 0); }
                    ImGui::SameLine();
                    if (ImGui::Button("Right")){ NudgeSpritePixelRect(viewed_sprite, atlas, 1, 0); }

                    // Same idea, but by the sprite's own full width/height
                    // instead of 1px -- for a sheet packed as an even grid,
                    // this jumps exactly one frame over, so it doubles as a
                    // quick way to flip through already-packed frames
                    // without touching the pixel rect fields above.
                    ImGui::TextUnformatted("Nudge (full frame):");
                    if (ImGui::Button("Frame Up")){ NudgeSpritePixelRect(viewed_sprite, atlas, 0, -viewed_sprite->height); }
                    ImGui::SameLine();
                    if (ImGui::Button("Frame Down")){ NudgeSpritePixelRect(viewed_sprite, atlas, 0, viewed_sprite->height); }
                    ImGui::SameLine();
                    if (ImGui::Button("Frame Left")){ NudgeSpritePixelRect(viewed_sprite, atlas, -viewed_sprite->width, 0); }
                    ImGui::SameLine();
                    if (ImGui::Button("Frame Right")){ NudgeSpritePixelRect(viewed_sprite, atlas, viewed_sprite->width, 0); }

                    ImGui::TextUnformatted("Cropped preview:");
                    if (ImGui::Button("Invert")){
                        InvertTextureRGB(atlas);
                    }

                    // This preview gets its own fit scale (based on the
                    // sprite's own width/height) rather than reusing
                    // preview_size above (which fits the FULL atlas) --
                    // reusing that would stretch a crop with a different
                    // aspect ratio to fill the same box.
                    const float max_crop_preview_px = 256.0f;
                    float crop_fit_scale = 1.0f;
                    float crop_largest_dim = (float)std::max(viewed_sprite->width, viewed_sprite->height);
                    if (crop_largest_dim > max_crop_preview_px){
                        crop_fit_scale = max_crop_preview_px / crop_largest_dim;
                    }
                    float crop_scale = (crop_preview_scale > 0.0f) ? crop_preview_scale : crop_fit_scale;
                    ImVec2 cropped_size((float)viewed_sprite->width * crop_scale, (float)viewed_sprite->height * crop_scale);

                    ImGui::Text("Showing %d x %d px at %.0f%%", viewed_sprite->width, viewed_sprite->height, crop_scale * 100.0f);
                    if (ImGui::Button("Fit")){ crop_preview_scale = -1.0f; }
                    ImGui::SameLine();
                    if (ImGui::Button("50%")){ crop_preview_scale = 0.5f; }
                    ImGui::SameLine();
                    if (ImGui::Button("100%")){ crop_preview_scale = 1.0f; }
                    ImGui::SameLine();
                    if (ImGui::Button("200%")){ crop_preview_scale = 2.0f; }

                    ImGui::Image(tex_id, cropped_size,
                        ImVec2(viewed_sprite->uv0.x, viewed_sprite->uv0.y),
                        ImVec2(viewed_sprite->uv1.x, viewed_sprite->uv1.y));
                }
            }
        }

    }
    ImGui::End();
}
#endif //USE_IMGUI
