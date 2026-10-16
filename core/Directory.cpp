#include <stdio.h>
#include "Directory.h"
#include "StringView.h"
#include "File.h"
#if defined(_WIN32)
#include <windows.h>
#else
#include <dirent.h>
#include <errno.h>
#include <fnmatch.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include "Debug.h"
static Debugger *debug = new Debugger("Directory", DEBUG_ALL);


#if defined(_WIN32)
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
#endif


std::vector<std::string> Directory::GetFiles(const char* dirname, const char* filetype){
    std::vector<std::string>files;

    /*
        The folder is an ASSET NAME like "icons", not a path, and it goes through the same search
        path everything else does - see core/File.h. Without this an app that keeps its icons in
        its own assets folder would find nothing, because the name only means something relative
        to a root.

        Resolving here rather than at the call site matters for what this returns: the names below
        are built as `dirname + "/" + file` and handed to Texture/LoadFile afterwards. Prefixing
        them with the RESOLVED folder makes each one a path that already exists, so LoadFile finds
        it on its as-given step and never has to resolve a second time.
    */
    std::string resolved_dir;
    if (!ResolveAssetDirectory(dirname,resolved_dir)){
        debug->Err("GetFiles: no such folder [%s] - looked in: %s\n",dirname,resolved_dir.c_str());
        return files;
    }
    dirname = resolved_dir.c_str();

#if defined(_WIN32)
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
#else
    //fnmatch takes the same "*.png" wildcard, but is case-sensitive where FindFirstFile is not - as
    //is the filesystem under it, so a pattern matches here exactly what a later open would find.
    DIR* dir = opendir(dirname);
    if (!dir){
        debug->Err("No first file in folder %s\n",dirname);
        return files;
    }
    while (struct dirent* entry = readdir(dir)){
        std::string name = std::string(dirname) + "/" + entry->d_name;
        struct stat st;
        if (stat(name.c_str(),&st) != 0 || S_ISDIR(st.st_mode)){
            continue;
        }
        if (fnmatch(filetype,entry->d_name,0) == 0){
            files.push_back(name);
            debug->Info("%s - %lld bytes\n", entry->d_name, (long long)st.st_size);
        }
    }
    closedir(dir);
#endif
    return files;
}

std::vector<std::string> Directory::ListFiles(const std::string& path, const char* suffix){
    std::vector<std::string> names;
    std::string end = suffix ? suffix : "";
#if defined(_WIN32)
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((path + "/*" + end).c_str(),&fd);
    if (h != INVALID_HANDLE_VALUE){
        do{
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)){
                names.push_back(fd.cFileName);
            }
        }while (FindNextFileA(h,&fd));
        FindClose(h);
    }
#else
    DIR* dir = opendir(path.c_str());
    if (!dir){
        return names;
    }
    while (struct dirent* entry = readdir(dir)){
        std::string name = entry->d_name;
        if (name.size() < end.size() || name.compare(name.size() - end.size(),end.size(),end) != 0){
            continue;
        }
        struct stat st;
        if (stat((path + "/" + name).c_str(),&st) == 0 && !S_ISDIR(st.st_mode)){
            names.push_back(name);
        }
    }
    closedir(dir);
#endif
    return names;
}

bool Directory::Create(const std::string& path){
#if defined(_WIN32)
    return CreateDirectoryA(path.c_str(),NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
#else
    if (mkdir(path.c_str(),0755) == 0){
        return true;
    }
    struct stat st;
    return errno == EEXIST && stat(path.c_str(),&st) == 0 && S_ISDIR(st.st_mode);
#endif
}

std::string Directory::FullPath(const std::string& path){
#if defined(_WIN32)
    char full[MAX_PATH];
    DWORD len = GetFullPathNameA(path.c_str(),MAX_PATH,full,NULL);
    return (len > 0 && len < MAX_PATH) ? std::string(full) : path;
#else
    /*
        Worked out from the TEXT, as GetFullPathName does, rather than by realpath: realpath asks the
        filesystem and fails for a folder not made yet, which is exactly what a bug report's folder
        is when its path is first shown.
    */
    std::string absolute = path;
    if (absolute.empty() || absolute[0] != '/'){
        char cwd[PATH_MAX];
        if (!getcwd(cwd,sizeof(cwd))){
            return path;
        }
        absolute = std::string(cwd) + "/" + path;
    }
    std::vector<std::string> parts;
    size_t pos = 0;
    while (pos <= absolute.size()){
        size_t slash = absolute.find('/',pos);
        if (slash == std::string::npos){
            slash = absolute.size();
        }
        std::string part = absolute.substr(pos,slash - pos);
        if (part == ".."){
            if (!parts.empty()){
                parts.pop_back();
            }
        }else if (!part.empty() && part != "."){
            parts.push_back(part);
        }
        pos = slash + 1;
    }
    std::string out;
    for (const std::string& part : parts){
        out += "/" + part;
    }
    return out.empty() ? "/" : out;
#endif
}
