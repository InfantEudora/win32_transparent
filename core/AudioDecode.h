#pragma once
/*
    Compressed audio, decoded to PCM16 once, at load - the way a PNG becomes a texture. The two
    loaders (SoundSystem::AppendFile and LoadMusicSampleFile) keep reading wavs themselves and
    come here only for what is not one.

    OGG VORBIS ONLY, through stb_vorbis (3rdparty/miniaudio/extras/stb_vorbis.c). It is what the
    archer's music samples ship as: 66 MB of wav became 2 MB at 32 kHz and -q -1, and nobody could
    hear the difference (2026-09-30; apps/music/makefile's publish target does the encoding).

    Loaders SNIFF the bytes rather than trust the extension, so a score that still says .wav for a
    file that is now Ogg - or the reverse - loads what the file actually is.

    Nothing is streamed: the whole file becomes PCM and the compressed bytes can go. Memory costs
    what the wav did (less, at the lower rate); what shrinks is the disk and the baked exe.
*/
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

//True if these bytes start an Ogg stream ("OggS"). Says nothing about whether it is Vorbis.
bool IsOggStream(const void* bytes, size_t size);

//The whole of an Ogg Vorbis file as interleaved PCM16. False, with `error` saying why, if
//stb_vorbis cannot read it - an Ogg Opus or FLAC file lands here too.
bool DecodeOggVorbis(const void* bytes, size_t size, std::vector<int16_t>& pcm, int& channels, int& rate, std::string& error);
