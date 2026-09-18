#ifndef _SPRITE_PACKER_APPLICATION_H_
#define _SPRITE_PACKER_APPLICATION_H_

#include <memory>
#include <string>
#include <vector>

#include "Application.h"
#include "SpriteSheet.h"
#include "Texture.h"

// Windows GUI tool for building/previewing sprite sheets -- reuses
// core/Application the same way hello_world/sprite_demo do on Android (see
// core/Application.h's class comment), just entered via the Windows
// Init()/Tick()/Shutdown()/HandleWindowMessage() hooks (see
// src/main.cpp) instead of Run(struct android_app*).
class SpritePackerApplication : public Application {
public:
    SpritePackerApplication();

protected:
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

    // Re-lists folder_path_buf's contents into file_list (filtered to
    // *.png -- these are the only files this tool actually loads/packs),
    // or sets file_list_error and clears file_list on failure. Always ends
    // by calling LoadImages(), so a folder change/refresh re-loads
    // everything in one step. Called whenever folder_path_buf changes
    // (typed Enter, or a successful BrowseForFolder()) or Refresh is clicked.
    void RefreshFileList();

    // Native Windows folder-picker dialog (IFileOpenDialog, FOS_PICKFOLDERS)
    // -- on a successful pick, copies the result into folder_path_buf and
    // calls RefreshFileList(). No-op (leaves folder_path_buf untouched) if
    // the user cancels or the dialog fails.
    void BrowseForFolder();

    // Loads every name in file_list from folder_path_buf into its own
    // Texture (loaded_textures), then points sprite_sheet at all of them
    // via AddSpriteFromWholeTexture() -- see that method's comment for why
    // this doesn't pack them into one shared atlas. A file that fails to
    // decode is skipped (already logged by Texture::LoadFromFile()), so
    // sprite_sheet.sprites can end up shorter than file_list -- code
    // matching a list entry back to its Sprite should look it up by name
    // (SpriteSheet::GetSprite(const char*)), not assume the same index.
    // Drops whatever was previously loaded first, every time.
    void LoadImages();

    // Loads path as one more sprite, appended to whatever's already loaded
    // (does NOT go through RefreshFileList()/LoadImages()'s clear-and-reload
    // -- a dropped file is additive, not a folder switch). Same skip-on-
    // decode-failure behavior as LoadImages(); named by its own filename,
    // not qualified with folder_path_buf, same as every other sprite.
    // Becomes the viewed/slider sprite on success. Called from RenderUI()'s
    // import-confirmation modal.
    void ImportDroppedFile(const std::string& path);

    char folder_path_buf[512] = "";
    std::vector<std::string> file_list; // just filenames (*.png only), not full paths -- see RefreshFileList()
    std::string file_list_error;        // empty if the last RefreshFileList() succeeded

    std::vector<std::unique_ptr<Texture>> loaded_textures; // one per successfully-loaded file_list entry
    SpriteSheet sprite_sheet;                               // sprites reference loaded_textures, see LoadImages()

    int slider_index = 0; // live "Sprite" slider position -- can move freely without changing viewed_index
    int viewed_index = -1; // which sprite the preview/UV editor below actually shows -- only updated when the slider is released (or a file-list entry is clicked)

    // Zoom level for the cropped-preview image specifically (independent of
    // the full-atlas preview above it, which always auto-fits). <= 0 means
    // "Fit" (auto-computed from the current sprite's own pixel size each
    // frame); otherwise an explicit factor set by the 50%/100%/200% buttons.
    float crop_preview_scale = -1.0f;

    bool f_filemodal = false;
    std::string filemodal_filename;
};

#endif
