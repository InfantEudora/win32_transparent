#ifndef _DIRECTORY_OPS_H_
#define _DIRECTORY_OPS_H_

#include <stddef.h>
#include <stdint.h>
#include <vector>
#include <string>
/*
    Since C++ provides no way to do anyting with directories (no... fs::filesystem is not a thing apparently.)
    We use the WIN32 API and wrap that to be more sensible.

    - Need to convert "dirname/" to "\dirname".
    - Lookup with a wildcard or a file extension.
*/


class Directory{
    public:

    static std::vector<std::string> GetFiles(const char* dirname, const char* filetype);
};


#endif