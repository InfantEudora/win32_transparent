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

    //Every file in an ASSET folder ("icons") matching a wildcard ("*.png"), each named folder + "/" + file.
    static std::vector<std::string> GetFiles(const char* dirname, const char* filetype);

    /*
        The rest take a real PATH, not an asset name - a save folder, the recordings beside the exe.
        One version each here rather than an #ifdef at every caller, which is what there was: the
        same FindFirstFile loop in four files, each of them doing nothing at all off Windows.
    */
    //The bare names (no folder) of the files in `path` ending in `suffix` (".json"; "" for all), folders skipped.
    static std::vector<std::string> ListFiles(const std::string& path, const char* suffix);
    //Makes one folder. True if it is there afterwards, made now or already.
    static bool Create(const std::string& path);
    //Absolute, with "." and ".." worked out - for SHOWING a path to a person. The path as given if that fails.
    static std::string FullPath(const std::string& path);
};


#endif