#include "ChasmVersion.h"

//THE NUMBER, raised by hand when the user says so. Only this file and the exe's version resource are rebuilt for
//it: the makefile reads it from here for chasm.rc, which also wants it as numbers - so digits and dots only.
#define CHASM_VERSION "0.0.1"

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
