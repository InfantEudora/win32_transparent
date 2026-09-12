#ifndef _FILE_OPS_H_
#define _FILE_OPS_H_

#include <stddef.h>
#include <stdint.h>
#include <string>
#include "BinaryAsset.h"

std::string GetBasePath(const char* filename);

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