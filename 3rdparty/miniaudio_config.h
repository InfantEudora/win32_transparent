#pragma once
/*
    miniaudio's feature set for this engine, in ONE place because it has to be.

    WHY THIS FILE EXISTS RATHER THAN A LIST OF -D IN THE MAKEFILES.

    Several of these defines change struct layout, not just which functions get compiled.
    MA_NO_RESOURCE_MANAGER removes `ma_resource_manager* pResourceManager` from the middle of
    `struct ma_engine` (miniaudio.h, in the engine struct), so every member after it shifts.
    A translation unit that included miniaudio.h without this file would agree with the
    library about the NAME of every field and disagree about where it lives - and that is
    not a link error, it is memory corruption at run time, in an audio callback, on another
    thread. It would be found weeks later.

    So the defines live here, this file is included before miniaudio.h everywhere without
    exception, and 3rdparty/makefile passes NO -DMA_* on the command line. If you need to
    change the feature set, change it here and rebuild libthirdparty.a; there is nowhere
    else to keep in step.

    WHAT IS TURNED OFF, AND WHY IT IS SAFE TO TURN OFF.

    Everything that reads or writes an audio FILE. core/WaveFile.cpp already parses the only
    format this engine loads and hands over PCM16 in memory, so miniaudio's decoders, its
    encoders, and the resource manager - which exists to build sounds from filenames - are
    all dead weight. Removing the resource manager leaves ma_sound_init_from_data_source(),
    which is what an in-memory ma_audio_buffer wants anyway.

    WHAT IS DELIBERATELY KEPT.

    The engine and the node graph. They cost 30,620 bytes over a device-only build and they
    are what mixes several voices at once and provides per-voice volume, looping and a
    playing/stopped query - the entire surface core/SoundSystem.cpp needs. Writing that by
    hand in a device callback to save thirty kilobytes would be a bad trade.

    MEASURED 2026-09-13, code+data of the implementation object at -Os:

        stock, no defines                                390,260
        this file                                        172,392
        this file + MA_NO_ENGINE + MA_NO_NODE_GRAPH      141,772

    For comparison, libs/libOpenAL32.a costs about 2,515,968 bytes against a probe calling
    only the fifteen AL functions core/SoundSystem.cpp uses. See docs/engine_backlog.md
    items 80 and 85.
*/

/* One backend: the one Windows has. Without this, DirectSound and WinMM are compiled too. */
#define MA_ENABLE_ONLY_SPECIFIC_BACKENDS
#define MA_ENABLE_WASAPI

/* No file I/O of any kind - WaveFile.cpp does that. Note MA_NO_DECODING already implies
   MA_NO_RESOURCE_MANAGER inside miniaudio.h; it is spelled out here so the layout
   consequence described above is visible at the point of decision rather than implied. */
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_WAV
#define MA_NO_FLAC
#define MA_NO_MP3
#define MA_NO_RESOURCE_MANAGER

/* Tone and noise synthesis. Nothing here generates audio; it plays samples. */
#define MA_NO_GENERATION
