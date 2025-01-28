#ifndef _STRINGVIEW_H_
#define _STRINGVIEW_H_

#include <stdint.h>
#include <string.h>

//From nob: https://github.com/tsoding/nob.h
typedef struct {
    size_t      count;
    uint8_t*    data;
}StringView;

//Helpers
StringView  sv_chop_by_delim(StringView* sv, char delim);
StringView  sv_from_parts(uint8_t* data, size_t count);
bool        sv_eq(StringView a, StringView b);
StringView  sv_from_cstr(const char *cstr);
bool        sv_end_with(StringView& sv, const char *cstr);
#endif

