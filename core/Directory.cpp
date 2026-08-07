#include <stdio.h>
#include "Directory.h"
#include "StringView.h"
#include <windows.h>
#include "Debug.h"
static Debugger *debug = new Debugger("Directory", DEBUG_ALL);


std::string SensiblePathToWin32Path(const char* dirname){
    char* c = (char*)dirname;
    std::string path;
    while(*c != '\0'){
        if (*c == '/'){
            path += '\\';
        }else{
            path += *c;
        }
        c++;
    }
    return path;
}


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
    std::string win32path = SensiblePathToWin32Path(dirname);
    win32path += "\\";
    win32path += filetype;
    LARGE_INTEGER filesize;
    hFind = FindFirstFile(win32path.c_str(), &ffd);

    if (INVALID_HANDLE_VALUE == hFind){
        debug->Err("No first file in folder %s\n",win32path.c_str());
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


