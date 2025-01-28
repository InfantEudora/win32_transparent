#include <stdio.h>
#include "Directory.h"
#include "StringView.h"

#include "Debug.h"
static Debugger *debug = new Debugger("Directory", DEBUG_ALL);


std::vector<std::string> Directory::GetFiles(const char* dirname, const char* filetype){
    std::vector<std::string>files;


    /*
    TODO:
     Parse the actual dirname.
     Convert maybe with StringView to have backslashes winapified
     Check on trailing slashes.
    */

    // It'd be nice if std::filesystem actually existed
    WIN32_FIND_DATA ffd;
    HANDLE hFind = INVALID_HANDLE_VALUE;
    const char* dir = "data\\icons\\\\*.png"; // Look at this absolute mess
    LARGE_INTEGER filesize;
    hFind = FindFirstFile(dir, &ffd);

    if (INVALID_HANDLE_VALUE == hFind){
        debug->Err("No first file in folder %s\n",dir);
    }else{
        do{
            if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY){
                debug->Info("%s   <DIR>\n", ffd.cFileName);
            }else{
                std::string name = dirname;
                name += "/" + std::string(ffd.cFileName);

                files.push_back(name);
                filesize.LowPart = ffd.nFileSizeLow;
                filesize.HighPart = ffd.nFileSizeHigh;
                debug->Info("%s - %ld bytes\n", ffd.cFileName, filesize.QuadPart);
            }
        }while (FindNextFile(hFind, &ffd) != 0);

    }
    return files;
}


