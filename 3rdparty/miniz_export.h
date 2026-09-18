/*
    miniz's configuration, and the only place in this project that holds it.

    3rdparty/miniz is a submodule now (see .gitmodules), so the knobs cannot be set the way
    miniz documents them - by editing the #define block near the top of miniz.h. They were
    until 2026-09-18: the copy that stood here was 3.1.0 with five of those defines switched
    on by hand, plus two edits to miniz_tdef.c to make the result compile. Every one of them
    is a change a `git submodule update` would now revert without a word.

    They live here instead because miniz.h includes "miniz_export.h" BEFORE it reads its own
    config block, and miniz_common.h includes it before anything else. Every translation unit
    that reaches miniz - its own three .c files, and core/BinaryAsset.h for the engine and the
    tools - therefore passes through this file first, and none of them can end up compiled
    against a different configuration. That last part is the point rather than a bonus: these
    defines change which declarations exist, so a TU that disagreed with the library would
    fail at link time if we were lucky. The same defines passed as -D in the three makefiles
    that build or include miniz would work exactly as well and be three places to keep in
    step; this is one, and it is the one upstream already routes everything through.

    Upstream has no miniz_export.h in its source tree at all - CMake generates one, holding
    the dllexport attributes. Nothing here is a DLL, so an empty MINIZ_EXPORT is the whole of
    what that half of the file needs to say.
*/
#pragma once

//Nothing here is a DLL. CMake's generated version of this file puts __declspec() here.
#define MINIZ_EXPORT

//Read by the #error in core/BinaryAsset.h: if an upstream ever stops including this header,
//that check fails loudly instead of the build quietly getting a differently configured miniz.
#define MINIZ_CONFIGURED 1

/*
    The configuration proper - the same five the vendored copy had edited into miniz.h, so
    this is not a new set of choices, just a new home for them.

    MINIZ_NO_ZLIB_COMPATIBLE_NAMES is the one that is not merely about size. Without it
    miniz.h defines compress, uncompress, crc32, deflate, inflate and z_stream at namespace
    scope in every TU that includes core/BinaryAsset.h, which is most of the engine.

    The four above it are: the engine deflates and inflates raw blocks and does nothing else.
    It has no use for the ZIP container, for zlib's streaming z_stream, or for the file times
    the archive writer stamps - and MINIZ_NO_TIME is what keeps <time.h> and its Windows
    friends out of a header this many files include.
*/
#define MINIZ_NO_TIME
#define MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_ARCHIVE_WRITING_APIS
#define MINIZ_NO_ZLIB_APIS
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
