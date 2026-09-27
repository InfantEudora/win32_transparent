#pragma once
#include <string>

//Decodes `in` (wav/flac/mp3) and writes it to `out` as a PCM16 wav at its own rate and channel
//count - the format core/WaveFile loads. `f_trim` cuts the silence off both ends. See export.cpp.
bool ExportWav(const std::wstring& in, const std::wstring& out, bool f_trim, std::string& error);
