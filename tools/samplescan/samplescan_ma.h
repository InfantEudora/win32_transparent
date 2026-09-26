#pragma once
/*
    miniaudio for samplescan, and DELIBERATELY NOT 3rdparty/miniaudio_config.h.

    The engine's config turns every decoder off, because an app only ever plays the PCM16 wavs
    WaveFile.cpp parses. This tool exists to read whatever a sample pack happens to ship - mp3
    from ElevenLabs, flac from a library, 24-bit and float wavs - so it needs exactly the half
    the engine throws away, and none of the half it keeps (no device, no engine, no mixer).

    That is only safe because nothing here touches libthirdparty.a or core/SoundSystem: this
    tool compiles its own miniaudio implementation (samplescan_ma.cpp) and links no other copy.
    The layout hazard miniaudio_config.h describes is between two builds of miniaudio in ONE
    program; there is one build in this program, and every file that includes miniaudio.h gets
    it through this header, which is the same one-place rule applied locally.
*/
#define MA_NO_DEVICE_IO
#define MA_NO_THREADING
#define MA_NO_ENGINE
#define MA_NO_NODE_GRAPH
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_ENCODING
#define MA_NO_GENERATION

#include "miniaudio/miniaudio.h"
