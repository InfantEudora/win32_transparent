/*
    The miniaudio implementation, built into libs/libthirdparty.a.

    This replaces upstream's own miniaudio.c, which is the same two lines without the config
    include. It has to be ours so that miniaudio_config.h is guaranteed to be seen first:
    the library and every consumer must agree on the feature set, because some of those
    defines change struct layout. See the comment in miniaudio_config.h.

    3rdparty/miniaudio itself is a git submodule and is left untouched.
*/
#include "miniaudio_config.h"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio/miniaudio.h"
