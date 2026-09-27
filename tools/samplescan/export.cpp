#include "export.h"
#include "samplescan_ma.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

/*
    Writes any file samplescan can read as the one format the engine loads: a PCM16 wav, at the
    file's own rate and channel count. core/WaveFile parses exactly that and nothing else, and the
    engine's miniaudio has its decoders compiled out (3rdparty/miniaudio_config.h) - so an mp3 in
    the library has to become a wav before a game can play it, and this is where it does.

    `f_trim` cuts the silence off both ends, which matters more than it sounds for a NOTE: the
    music engine starts a note by starting its sample, so 50 ms of dead air in front of a kalimba
    is every kalimba note 50 ms late. The threshold is the analyser's (45 dB under the peak), and
    5 ms before the sound is kept so the attack itself is not clipped.
*/
bool ExportWav(const std::wstring& in, const std::wstring& out, bool f_trim, std::string& error){
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_s16, 0, 0);
    ma_decoder dec;
    if (ma_decoder_init_file_w(in.c_str(), &cfg, &dec) != MA_SUCCESS){
        error = "cannot decode";
        return false;
    }
    const int channels = (int)dec.outputChannels;
    const int rate = (int)dec.outputSampleRate;
    std::vector<int16_t> pcm;
    std::vector<int16_t> chunk(4096 * channels);
    for (;;){
        ma_uint64 got = 0;
        ma_result r = ma_decoder_read_pcm_frames(&dec, chunk.data(), 4096, &got);
        pcm.insert(pcm.end(), chunk.begin(), chunk.begin() + got * channels);
        if (r != MA_SUCCESS || got == 0) break;
    }
    ma_decoder_uninit(&dec);
    if (pcm.empty() || channels <= 0){
        error = "no audio";
        return false;
    }

    size_t first = 0, last = pcm.size() / channels;     //frames, [first, last)
    if (f_trim){
        const size_t frames = pcm.size() / channels;
        const size_t hop = std::max(1, rate / 100);
        auto frame_db = [&](size_t f0){
            double sq = 0;
            size_t n = 0;
            for (size_t f = f0; f < std::min(frames, f0 + hop); f++){
                for (int c = 0; c < channels; c++){ const double s = pcm[f * channels + c] / 32768.0; sq += s * s; n++; }
            }
            return n ? 10.0 * std::log10(sq / n + 1e-20) : -200.0;
        };
        double peak = -200;
        for (size_t f = 0; f < frames; f += hop) peak = std::max(peak, frame_db(f));
        const double threshold = std::max(peak - 45.0, -65.0);
        size_t a = 0, b = frames;
        while (a < frames && frame_db(a) < threshold) a += hop;
        while (b > a + hop && frame_db(b - hop) < threshold) b -= hop;
        first = a > (size_t)(rate / 200) ? a - rate / 200 : 0;
        last = std::min(frames, b + hop);
    }

    FILE* f = _wfopen(out.c_str(), L"wb");
    if (!f){
        error = "cannot write";
        return false;
    }
    const uint32_t data_bytes = (uint32_t)((last - first) * channels * 2);
    auto u32 = [&](uint32_t v){ fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v){ fwrite(&v, 2, 1, f); };
    fwrite("RIFF", 1, 4, f); u32(36 + data_bytes); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); u32(16); u16(1); u16((uint16_t)channels); u32((uint32_t)rate);
    u32((uint32_t)(rate * channels * 2)); u16((uint16_t)(channels * 2)); u16(16);
    fwrite("data", 1, 4, f); u32(data_bytes);
    fwrite(pcm.data() + first * channels, 2, (last - first) * channels, f);
    fclose(f);
    printf("  %d ch, %d Hz, %.2f s%s\n", channels, rate, (double)(last - first) / rate,
           f_trim ? " (trimmed)" : "");
    return true;
}
