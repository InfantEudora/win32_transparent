#ifndef _FILE_OPS_H_
#define _FILE_OPS_H_

#include <stddef.h>
#include <stdint.h>
#include "BinaryAsset.h"

std::string GetBasePath(const char* filename);

//Generic file functions
uint8_t* LoadFile(const char* filename, size_t* size, bool bypass_cache = false);

#endif