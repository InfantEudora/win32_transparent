//The one miniaudio implementation in this tool - see samplescan_ma.h for why it is not the engine's.

/*
    _CRTIMP empty, so the C runtime is declared as plain functions rather than DLL imports, in
    this file only. MinGW's wchar.h declares wcsrtombs dllimport, but msvcrt.dll does not export
    it - the only definition is the static one in libmingwex - so miniaudio's wide-path code
    (ma_path_extension_equal_w) otherwise fails to link on `__imp_wcsrtombs`. The engine's build
    never meets this because it compiles the decoders out. Every other CRT function still
    resolves, through the plain-name thunks the import library carries alongside __imp_.
*/
#define _CRTIMP

#define MINIAUDIO_IMPLEMENTATION
#include "samplescan_ma.h"
