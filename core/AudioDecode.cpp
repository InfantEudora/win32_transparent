#include "AudioDecode.h"

#include <cstdlib>
#include <cstring>

/*
    stb_vorbis compiled into this one object, not into libs/libthirdparty.a: it goes wherever
    sound goes (engine.mk drops this file with USE_SOUND=0), and adding it cost no rebuild of a
    library every app links. It builds clean as C++ under the engine's flags, about 31 KB of code.

    No stdio - bytes come from LoadFile or ReadFileToString, which is also what makes a baked
    asset work. No pushdata API - nothing here feeds it a packet at a time.
*/
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "miniaudio/extras/stb_vorbis.c"

bool IsOggStream(const void* bytes, size_t size){
    return bytes && size >= 4 && memcmp(bytes, "OggS", 4) == 0;
}

bool DecodeOggVorbis(const void* bytes, size_t size, std::vector<int16_t>& pcm, int& channels, int& rate, std::string& error){
    if (!IsOggStream(bytes, size) || size > 0x7fffffff){
        error = "not an Ogg file";
        return false;
    }
    short* out = nullptr;
    channels = 0;
    rate = 0;
    const int frames = stb_vorbis_decode_memory((const unsigned char*)bytes, (int)size, &channels, &rate, &out);
    if (frames < 0 || !out || channels <= 0 || rate <= 0){
        free(out);
        error = "an Ogg file, but not Vorbis stb_vorbis can read";
        return false;
    }
    pcm.assign(out, out + (size_t)frames * channels);
    free(out);      //stb_vorbis mallocs it
    return true;
}
