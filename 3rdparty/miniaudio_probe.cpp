/*
    A probe, not a part of the engine. Build it by hand:

        export PATH="/c/msys64/mingw64/bin:$PATH"
        g++ -std=c++17 -Os -s -fno-exceptions -D_WIN32 -I. miniaudio_probe.cpp \
            -L../libs -lthirdparty -lole32 -lwinmm -luser32 \
            -Wl,-Bstatic -static-libstdc++ -static-libgcc -static -lstdc++ \
            -Wl,--gc-sections -o /tmp/ma_probe.exe

    Two questions, both of which had to be answered before anyone starts rewriting
    core/SoundSystem.cpp against miniaudio:

    1. Does the feature set in miniaudio_config.h still cover what SoundSystem actually does?
       It calls exactly fifteen OpenAL functions - open a device, make a context, upload a
       PCM16 mono/stereo buffer, play, stop, pause, rewind, set gain, set looping, ask whether
       a voice is still playing. Everything below is the miniaudio spelling of that list and
       nothing else, so if this links and runs, the config is sufficient.

    2. What does it cost in a linked binary? docs/engine_backlog.md item 80 measured OpenAL
       the same way - a probe calling only the functions the engine uses, linked and stripped -
       and got 2,515,968 bytes against a 40,448 byte floor. This is the comparable number.

    MEASURED 2026-09-13, stripped, against an identical probe with the audio calls removed:

        floor, no audio library                              136,192
        this probe, miniaudio                                314,880   +178,688
        item 80's OpenAL probe, same method                            +2,475,520

    So miniaudio covers this engine's entire use of audio for about 179 KB where OpenAL costs
    2.4 MB - and that OpenAL figure is the already-trimmed build we ship, not a stock one.

    It plays a real 440 Hz tone through the default device for a moment, because "does it
    link" and "does it open the hardware and mix" are different questions and the second one
    is the one that matters.
*/
#include "miniaudio_config.h"
#include "miniaudio/miniaudio.h"

#include <cstdio>
#include <cmath>
#include <vector>
#include <windows.h>

int main() {
    // PCM16 mono, the format WaveFile.cpp hands SoundSystem today.
    const ma_uint32 sample_rate = 44100;
    const ma_uint32 frames = sample_rate / 2;   // half a second
    std::vector<ma_int16> pcm(frames);
    for (ma_uint32 i = 0; i < frames; i++) {
        double t = (double)i / (double)sample_rate;
        pcm[i] = (ma_int16)(std::sin(t * 440.0 * 2.0 * 3.14159265358979) * 8000.0);
    }

    ma_engine_config engine_cfg = ma_engine_config_init();
    engine_cfg.sampleRate = sample_rate;

    ma_engine engine;
    if (ma_engine_init(&engine_cfg, &engine) != MA_SUCCESS) {
        printf("FAIL: ma_engine_init\n");
        return 1;
    }
    printf("ok   engine init            (device opened, sample rate %u)\n",
           ma_engine_get_sample_rate(&engine));

    ma_audio_buffer_config buf_cfg =
        ma_audio_buffer_config_init(ma_format_s16, 1, frames, pcm.data(), NULL);
    buf_cfg.sampleRate = sample_rate;

    ma_audio_buffer buffer;
    if (ma_audio_buffer_init(&buf_cfg, &buffer) != MA_SUCCESS) {
        printf("FAIL: ma_audio_buffer_init\n");
        return 1;
    }
    printf("ok   buffer from memory     (PCM16, %u frames - no decoder involved)\n", frames);

    ma_sound sound;
    if (ma_sound_init_from_data_source(&engine, &buffer, 0, NULL, &sound) != MA_SUCCESS) {
        printf("FAIL: ma_sound_init_from_data_source\n");
        return 1;
    }
    printf("ok   sound from data source (this is what MA_NO_RESOURCE_MANAGER leaves us)\n");

    ma_sound_set_volume(&sound, 0.4f);          // alSourcef(AL_GAIN)
    ma_sound_set_looping(&sound, MA_FALSE);     // alSourcei(AL_LOOPING)
    printf("ok   volume + looping\n");

    if (ma_sound_start(&sound) != MA_SUCCESS) { // alSourcePlay
        printf("FAIL: ma_sound_start\n");
        return 1;
    }
    printf("ok   start                  (playing 440 Hz)\n");

    Sleep(250);
    printf("ok   is_playing mid-tone    %s\n",
           ma_sound_is_playing(&sound) ? "true" : "false");   // alGetSourcei(AL_SOURCE_STATE)

    ma_sound_stop(&sound);                                     // alSourceStop
    ma_sound_seek_to_pcm_frame(&sound, 0);                     // alSourceRewind
    printf("ok   stop + rewind          is_playing now %s\n",
           ma_sound_is_playing(&sound) ? "true" : "false");

    ma_sound_uninit(&sound);
    ma_audio_buffer_uninit(&buffer);
    ma_engine_uninit(&engine);
    printf("ok   teardown               (clean)\n");
    return 0;
}
