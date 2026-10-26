#include "ChasmVersion.h"
#include <algorithm>
#include <vector>

//THE NUMBER, raised by hand when the user says so. Only this file and the exe's version resource are rebuilt for
//it: the makefile reads it from here for chasm.rc, which also wants it as numbers - so digits and dots only.
#define CHASM_VERSION "0.0.3"

//The makefile's (THE VERSION there): passed in for this file alone. Built without it - another build system - the
//compiler's own date stands in, and the commit is not known.
#ifndef CHASM_GIT
#define CHASM_GIT "unknown"
#endif
#ifndef CHASM_BUILD_DATE
#define CHASM_BUILD_DATE __DATE__
#endif

const char* ChasmVersion(){
    return CHASM_VERSION;
}

const char* ChasmGitHash(){
    return CHASM_GIT;
}

const char* ChasmBuildDate(){
    return CHASM_BUILD_DATE;
}

//"0.0.3" as its numbers; false for anything else - a dot doubled or at an end, a letter.
static bool VersionNumbers(const std::string& version, std::vector<int>& out){
    out.assign(1,0);
    bool f_digit = false;
    for (char c : version){
        if (c >= '0' && c <= '9'){
            out.back() = out.back() * 10 + (c - '0');
            f_digit = true;
        }else if (c == '.' && f_digit){
            out.push_back(0);
            f_digit = false;
        }else{
            return false;
        }
    }
    return f_digit;
}

bool ChasmVersionOlder(const std::string& version){
    std::vector<int> theirs;
    std::vector<int> ours;
    if (!VersionNumbers(version,theirs) || !VersionNumbers(CHASM_VERSION,ours)){
        return true;
    }
    return std::lexicographical_compare(theirs.begin(),theirs.end(),ours.begin(),ours.end());
}
