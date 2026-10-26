#ifndef _CHASM_VERSION_H_
#define _CHASM_VERSION_H_

#include <string>

/*
    THE VERSION (the user's, 2026-10-16): a number of the user's own choosing - not semver, it says nothing about
    what changed - with the commit it was built from and the day it was built. The number is set in ChasmVersion.cpp;
    the commit and the day come from the makefile, which compiles that file again whenever either changes, so they
    are this build's own and not those of whenever the file was last edited.
*/
const char* ChasmVersion();         //"0.0.1"
const char* ChasmGitHash();         //"210d9e4", with a "+" when built with changes not yet committed; "unknown" outside git
const char* ChasmBuildDate();       //"2026-10-16"
//A version before this build's, number by number - and "" or anything not a version, which only saves from before saves
//said have: those are all older.
bool ChasmVersionOlder(const std::string& version);

#endif
