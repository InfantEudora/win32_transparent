#include "StringView.h"

StringView sv_chop_by_delim(StringView* sv, char delim){
    size_t i = 0;
    while (i < sv->count && (char)sv->data[i] != delim) {
        i += 1;
    }
    StringView result = sv_from_parts(sv->data, i);
    if (i < sv->count) {
        sv->count -= i + 1;
        sv->data  += i + 1;
    } else {
        sv->count -= i;
        sv->data  += i;
    }
    return result;
}

StringView sv_from_parts(uint8_t* data, size_t count){
    StringView sv;
    sv.count = count;
    sv.data = data;
    return sv;
}

bool nob_sv_eq(StringView a, StringView b){
    if (a.count != b.count) {
        return false;
    } else {
        return memcmp(a.data, b.data, a.count) == 0;
    }
}

StringView sv_from_cstr(const char *cstr){
    return sv_from_parts((uint8_t*)cstr, strlen(cstr));
}

bool sv_end_with(StringView& sv, const char *cstr){
    size_t cstr_count = strlen(cstr);
    if (sv.count >= cstr_count) {
        size_t ending_start = sv.count - cstr_count;
        StringView sv_ending = sv_from_parts(sv.data + ending_start, cstr_count);
        return nob_sv_eq(sv_ending, sv_from_cstr(cstr));
    }
    return false;
}