#ifndef _FILE_OPS_H_
#define _FILE_OPS_H_

#include <stddef.h>
#include <stdint.h>

//From nob
typedef struct {
    size_t      count;
    uint8_t*    data;
}StringView;

StringView sv_chop_by_delim(StringView* sv, char delim);
StringView sv_from_parts(uint8_t* data, size_t count);

//Generic file functions
uint8_t* LoadFile(const char* filename, size_t* size);

#endif