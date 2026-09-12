#ifndef _FILE_OPS_H_
#define _FILE_OPS_H_

#include <stddef.h>
#include <stdint.h>
#include <string>
#include "BinaryAsset.h"

std::string GetBasePath(const char* filename);

/*
    THE ASSET SEARCH PATH.

    A list of ROOT directories an asset name is looked up under, in order, exactly the way -I
    works for headers. An asset is named by its CATEGORY and file - "shaders/default.vert",
    "meshes/tank.glb" - never by where it physically lives, and the roots decide which copy of
    that name the process actually gets.

    Set from APP_ASSET_PATH, which apps/<App>.mk builds out of its APP_ASSETS list. So a typical
    app resolves against its own root and then the shared one:

        assets/ship,assets/shared

    and "shaders/default.vert" is found at assets/shared/shaders/default.vert while
    "shaders/raymarch_volume.frag" is found at assets/ship/shaders/raymarch_volume.frag.

    ORDER IS THE FEATURE. An app that wants its own take on a shared shader drops a file with the
    same name in its own root and nothing else changes - first match wins, and the app's root
    comes first. That is the whole reason this is a list and not a single directory.

    THE NAME AS GIVEN IS ALWAYS TRIED FIRST, before any root. Which means every path literal that
    predates this mechanism - "data/cityandroads.glb", "shaders/default.vert", relative to the working
    directory - still resolves exactly as it did, because the file is still there. Nothing has to
    move for this to be safe, and assets can be migrated one app at a time. When the last of them
    has moved, the old directories simply stop being found and the fallback costs one failed
    stat per asset load.
*/
void AddAssetSearchRoot(const char* root);

/*
    The directory the running executable sits in, with no trailing separator.

    Every path this engine resolves has historically been relative to the WORKING directory, which
    is whatever the thing that launched the process happened to be pointing at. That is fine while
    there is one exe built at the repo root and everyone runs it from there, and it stops being
    fine the moment each app is its own exe in its own build folder: launched from a debugger, a
    shortcut, or a script in another directory, the same binary would look for its assets
    somewhere else entirely and fail with nothing obviously wrong.
*/
std::string GetExecutableDirectory();

/*
    Adds a search root given RELATIVE TO THE EXECUTABLE rather than to the working directory, so
    an app can name its assets once and have them found however it was launched:

        AddAssetSearchRootFromExe("../assets");             //apps/tank/assets
        AddAssetSearchRootFromExe("../../../shared_assets");

    This is what an app's own main() should use. AddAssetSearchRoot stays for a root that really
    is meant to be working-directory relative, or one already absolute.
*/
void AddAssetSearchRootFromExe(const char* relative);

/*
    Turns an asset name into a path that exists on disk, following the search path above. True if
    something was found, and `out` is then the path to open. False if nothing matched, and `out`
    is left holding a description of everything that was tried - which is what a caller should put
    in its error message, because "not found" without the list of places looked is the least
    useful thing a file layer can say.

    Exposed because not every kind of asset access can go through LoadFile: a directory scan
    (Directory::GetFiles) needs the resolved FOLDER, and a library that wants to open a file
    itself needs the resolved path. Anything that just wants bytes should call LoadFile and let
    it do this.
*/
bool ResolveAssetPath(const char* name, std::string& out);

/*
    The same, for a DIRECTORY rather than a file - "icons" finding apps/sim/assets/icons.

    Separate from ResolveAssetPath rather than one function that accepts either, because the two
    answer different questions and a caller always knows which it is asking. A name can in
    principle be both a file and a folder in different roots, and silently handing back whichever
    turned up first would be a bug that only appears once someone adds an innocuous file.

    For Directory::GetFiles, which is how an app loads a whole folder of assets without naming
    each one - the icon sheets in Sim and Tileset are built that way.
*/
bool ResolveAssetDirectory(const char* name, std::string& out);

/*
    Hands back the bytes of a file.

    THE FILE LAYER OWNS THE BUFFER. DO NOT free() IT. You are being lent a pointer, and it stays
    valid for the life of the process. If you need bytes you can keep, change or outlive this -
    copy them. Copying is the caller's business, and it has to be: this function is the one thing
    that decides WHERE the bytes come from - a file on disk, the BinaryAsset cache, an asset baked
    into the executable, and tomorrow perhaps an archive or the network. It cannot know which of
    those you wanted a private copy of, and if it guessed by copying every time, then every caller
    that only wanted to read would pay for it and the cache would stop being a cache.

    One file, one buffer, one owner. A second load of the same name gets the same pointer as the
    first, not a duplicate.

    The buffer is one byte longer than `size` and that byte is zero, so it is safe to treat as a C
    string (GLSL source does).

    Returns NULL only if the file could not be read at all - and note that LoadFile treats that as
    FATAL and exits, so callers that want to handle a missing file gracefully want
    ReadFileToString instead.

    History, because the contract has flipped: before 2026-09-12 a cache hit returned the cache's
    own new[] pointer while the first load returned a fresh calloc, and three callers free()d what
    they got - a mismatched free that took the heap with it on the third load of any one file. The
    stopgap was to copy on every path so that "the caller owns it" became true everywhere. That
    made LoadFile allocate on behalf of callers who never asked, so it leaked once per cached load
    through the five callers that never freed. This is the version that settles it instead of
    patching it: nobody frees, because nobody but the file layer ever owned it.
*/
uint8_t* LoadFile(const char* filename, size_t* size);

/*
    Asks the file layer to let go of what it holds for `filename`, so that the next LoadFile of
    that name goes back to the file. This is how a hot reload picks up an edit: without it,
    LoadFile answers from the cache forever and a "reload" recompiles the bytes read at start-up.

    THREE ANSWERS, and the third is the point of the function:

      FILE_RELEASED          it was ours, it is freed, the next load reads the file again.
      FILE_RELEASE_FAILED    nothing is held under that name. Nothing happened.
      FILE_RELEASE_EMBEDDED  the asset is baked into the executable. It is not ours to free, and
                             there is no file behind it to re-read - a reload here can only ever
                             produce the same bytes.

    That last one is not an error and must not be reported as one. Packing every asset into the
    binary is the normal way this engine ships and the only way the Android port can work, so a
    feature built on this has to be able to say "not available in this build" rather than appear
    to work and quietly do nothing. Which is exactly the failure this function exists to end.

    DANGEROUS IN ONE SPECIFIC WAY. Everything else here promises that a pointer from LoadFile
    stays valid for the life of the process; this is the one thing that breaks that promise. Only
    release an asset you know nobody is still holding. Shader source is the case it was written
    for - Shader copies it into a std::string while compiling and keeps nothing afterwards. A
    Texture or a WaveFile, by contrast, holds its bytes for its whole life.
*/
FileRelease ReleaseFile(const char* filename);

/*
    Reads a file straight from disk into a string THE CALLER OWNS, with no caching on either side.

    For the two cases LoadFile deliberately does not serve: a file expected to CHANGE between
    reads (the cache has no way to be told that what it holds is stale), and a file that is
    allowed to be MISSING - this returns false where LoadFile calls Fatal and exits the process.

    Prefer LoadFile for anything that is really an asset. This one re-reads and re-allocates every
    single call, which is the point of it.
*/
bool ReadFileToString(const char* filename, std::string& out);

#endif