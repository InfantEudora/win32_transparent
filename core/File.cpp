#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#if defined(_WIN32)
#include <windows.h>        //GetModuleFileName - see GetExecutableDirectory
#else
#include <sys/stat.h>       //stat/S_ISDIR - see PathExists
#endif
#include "File.h"

#include "Debug.h"
static Debugger *debug = new Debugger("File", DEBUG_ALL);

//Only ever written by SetWritableDataDirectory, and only read on the non-Win32 arm of
//GetExecutableDirectory below.
static std::string writable_data_dir;

void SetWritableDataDirectory(const char* path){
	if (path && *path){
		writable_data_dir = path;
	}
}

std::string GetBasePath(const char* filename){
	std::string sname = filename;
  	std::size_t found = sname.find_last_of("/\\");
    if (found == -1){
        return "";
    }
  	return sname.substr(0,found);
}

/*
    APP_ASSET_PATH is written by the makefile out of the selected app's APP_ASSETS list, the same
    way APP_HEADER and APP_CLASS are. A build that does not set it gets an empty path and behaves
    exactly as this engine did before the search path existed: every name resolves as a path
    relative to the working directory, or not at all.
*/
#ifndef APP_ASSET_PATH
#define APP_ASSET_PATH ""
#endif

static std::vector<std::string> asset_roots;
static bool f_asset_roots_ready = false;

//Trailing separators are stripped so that joining is one rule rather than two - see ResolveAssetPath.
void AddAssetSearchRoot(const char* root){
	if (!root || !*root){
		return;
	}
	std::string r = root;
	while (!r.empty() && ((r.back() == '/') || (r.back() == '\\'))){
		r.pop_back();
	}
	if (r.empty()){
		return;
	}
	for (const std::string& existing:asset_roots){
		if (existing == r){
			return;         //declaring a root twice is harmless, searching it twice is just slow
		}
	}
	asset_roots.push_back(r);
}

/*
    Splits APP_ASSET_PATH into roots, the first time anything asks for a file.

    Lazily rather than in a constructor because the roots have to be in place before the FIRST
    asset load, and a static initialiser here would be racing every other translation unit's
    statics for that - including `debug` above, which this wants to log through. The first
    LoadFile is comfortably after main() starts, so lazy is both simpler and correct.

    A root added by hand before then keeps its place at the FRONT, ahead of the compiled-in ones,
    because this only ever appends. That is the useful way round: a root chosen at run time is
    more specific than one baked in at build time.
*/
static void InitAssetRoots(){
	if (f_asset_roots_ready){
		return;
	}
	f_asset_roots_ready = true;         //set first: AddAssetSearchRoot must not recurse back in here

	/*
	    Both ',' and ';' separate, because the makefile cannot use ';': it hands the compile and
	    link commands to sh, which would read a ';' in the define as a command separator and cut
	    the path in half - a link failure that names a directory and mentions nothing about a
	    define. ',' is what actually arrives. ';' is accepted anyway because it is what a Windows
	    PATH uses and so what anyone setting this by hand will reach for first.
	*/
	const char* path = APP_ASSET_PATH;
	std::string entry;
	for (const char* p = path;;p++){
		if ((*p == ',') || (*p == ';') || (*p == '\0')){
			AddAssetSearchRoot(entry.c_str());
			entry.clear();
			if (*p == '\0'){
				break;
			}
			continue;
		}
		entry += *p;
	}

	if (asset_roots.empty()){
		debug->Info("Asset search path is empty - names resolve against the working directory only\n");
		return;
	}
	for (size_t i = 0;i < asset_roots.size();i++){
		debug->Info("Asset search root %zu: %s\n",i,asset_roots[i].c_str());
	}
}

std::string GetExecutableDirectory(){
#if defined(_WIN32)
	//Worked out once. GetModuleFileName does not change over the life of the process, and the
	//roots built from it are read on every failed as-given lookup.
	static std::string dir;
	static bool f_done = false;
	if (f_done){
		return dir;
	}
	f_done = true;

	char buf[MAX_PATH] = {0};
	DWORD n = GetModuleFileNameA(NULL,buf,MAX_PATH);
	if ((n == 0) || (n >= MAX_PATH)){
		//Nothing useful to fall back to but the working directory, which is the old behaviour.
		debug->Err("GetExecutableDirectory: GetModuleFileName failed - exe-relative roots will be working-directory relative instead\n");
		return dir;
	}
	dir = GetBasePath(buf);
	return dir;
#else
	/*
		Set by Application from the platform's own answer - see SetWritableDataDirectory in File.h.
		Empty until that happens, and a caller that concatenates a filename onto an empty string
		writes to the working directory instead, which is exactly the fallback the Win32 failure
		path above takes. So an unset directory degrades the same way a failed query does.
	*/
	return writable_data_dir;
#endif
}

void AddAssetSearchRootFromExe(const char* relative){
	if (!relative || !*relative){
		return;
	}
	std::string base = GetExecutableDirectory();
	if (base.empty()){
		AddAssetSearchRoot(relative);       //see GetExecutableDirectory - degrade, do not drop it
		return;
	}
	/*
	    The ".." are left in rather than collapsed. Windows resolves them itself on every call that
	    takes a path, so a root like C:/repo/apps/tank/build/../assets opens exactly what the tidy
	    form would - and leaving them makes the logged root say plainly what it was built from,
	    which is the thing worth reading when an asset is not found.
	*/
	AddAssetSearchRoot((base + "/" + relative).c_str());
}

//Asks about a path without opening it, and answers for a directory as well as a file - resolution
//can try several candidates per asset and only one of them exists. fopen would do neither.
//
//Two implementations because no one call does this on both platforms. They must agree on one
//thing only: a path that is not there, and a path that is there but is the wrong KIND of thing,
//both answer false. ResolveAgainstRoots relies on that to keep walking to the next root.
#if defined(_WIN32)
static bool PathExists(const std::string& path, bool f_want_directory){
	DWORD attr = GetFileAttributesA(path.c_str());
	if (attr == INVALID_FILE_ATTRIBUTES){
		return false;
	}
	bool f_is_directory = (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
	return f_is_directory == f_want_directory;
}
#else
static bool PathExists(const std::string& path, bool f_want_directory){
	struct stat st;
	if (stat(path.c_str(),&st) != 0){
		return false;
	}
	bool f_is_directory = S_ISDIR(st.st_mode);
	return f_is_directory == f_want_directory;
}
#endif

//The search itself. ResolveAssetPath and ResolveAssetDirectory are the same walk asking for a
//different kind of thing at the end of it - see File.h for why they stay two functions.
static bool ResolveAgainstRoots(const char* name, std::string& out, bool f_want_directory){
	out.clear();
	if (!name || !*name){
		return false;
	}
	InitAssetRoots();

	/*
	    The name exactly as given comes first, and that ordering is what makes this safe to add to
	    a tree full of working path literals: "data/cityandroads.glb" is found where it has always been,
	    before a single root is consulted. It is also what lets a caller pass a path it resolved
	    earlier - or an absolute one - straight back in without it being mangled.
	*/
	if (PathExists(name,f_want_directory)){
		out = name;
		return true;
	}

	for (const std::string& root:asset_roots){
		std::string candidate = root + "/" + name;
		if (PathExists(candidate,f_want_directory)){
			out = candidate;
			return true;
		}
	}

	//Everything that was tried, in order, so the caller's error message can say where it looked.
	//A bare "not found" sends people hunting for a file that is present but under a root nobody
	//declared, which is the one failure this mechanism newly makes possible.
	out = name;
	for (const std::string& root:asset_roots){
		out += ", ";
		out += root + "/" + name;
	}
	return false;
}

bool ResolveAssetPath(const char* name, std::string& out){
	return ResolveAgainstRoots(name,out,false);
}

bool ResolveAssetDirectory(const char* name, std::string& out){
	return ResolveAgainstRoots(name,out,true);
}

/*
    Finds a file's bytes, wherever they happen to live, and lends them to the caller. See File.h
    for the ownership rule, which is the whole point of this function: what comes back belongs to
    the file layer and nobody frees it.

    Deciding where the bytes come from is this function's job and nobody else's. Today that is the
    BinaryAsset table (which holds both what has already been read off disk and what was baked
    into the executable), and failing that, the disk. A caller that wanted to know which it got
    would have to care about a distinction that exists so it does not have to.
*/
uint8_t* LoadFile(const char* filename, size_t* size){
	/*
	    CACHED UNDER THE NAME AS GIVEN, NEVER UNDER THE PATH IT RESOLVED TO. The search path is
	    consulted below only to find bytes on a disk; it must not reach the key, and the reason is
	    the packed build. An asset baked into the executable is looked up here by the name the
	    caller asked for, because that is all a packed build has - there is no disk to have
	    resolved against. If the cache key were the resolved path, the same "shaders/default.vert"
	    would key as "assets/shared/shaders/default.vert" in a loose build and as itself in a
	    packed one, and the two builds would silently disagree about what is already loaded.
	    Keying on the name as given also means ReleaseFile - which only ever sees the name - stays
	    able to find what LoadFile stored.
	*/
	BinaryAsset* asset = BinaryAsset::GetBinaryAsset(filename);
	if (!asset){
		std::string resolved;
		if (!ResolveAssetPath(filename,resolved)){
			/*
			    Err, NOT Fatal, and the difference is the whole failure policy of this function.
			    Fatal is exit(1) (see Debug.cpp), which kills the process before anything can report
			    what was being loaded or carry on without it. A missing asset is loud enough on its
			    own: every caller here checks for NULL and says what it could not do, so the app comes
			    up with one thing missing and a log line naming it - far easier to act on than a
			    process that vanished at startup.

			    It matters most where it is least recoverable. On Android there is no disk to fall
			    back to and no console to read: an exit(1) there is an app that closes itself on
			    launch, which looks like a crash and gets debugged like one.
			*/
			debug->Err("LoadFile failed to load [%s] - looked in: %s\n",filename,resolved.c_str());
			return NULL;
		}

		FILE* file = fopen(resolved.c_str(), "rb");
		if(!file){
			//Resolution just proved this openable, so arriving here means it went away in between.
			//Err rather than Fatal, for the reason given above.
			debug->Err("LoadFile failed to load: [%s] (resolved to %s)\n",filename,resolved.c_str());
			return NULL;
		}

		/*get filesize:*/
		fseek(file , 0 , SEEK_END);
		size_t sz = ftell(file);
		rewind(file);

		//Both names when they differ: which root answered is the first thing anyone debugging an
		//override or a half-migrated app needs, and it is invisible from the call site.
		//(%zu, not %li - size_t is 64-bit here and long is not, so %li truncated it.)
		if (resolved == filename){
			debug->Info("LoadFile: File %s is %zu bytes\n",filename,sz);
		}else{
			debug->Info("LoadFile: File %s -> %s is %zu bytes\n",filename,resolved.c_str(),sz);
		}

		//calloc sz+1 so the last byte is a zero: that terminator is part of what StoreBinaryAsset
		//is promised, and it is what lets a caller hand this straight to something expecting a C
		//string without knowing whether it came off the disk or out of the cache.
		uint8_t* data = (uint8_t*)calloc(sz+1,1);
		if (!data){
			fclose(file);
			debug->Fatal("LoadFile: out of memory reading %s\n",filename);
			return NULL;
		}
		fread(data, 1, sz, file);
		fclose(file);

		//The cache ADOPTS the buffer - it is not copied - so what comes back is the one and only
		//copy of this file, and the next load of the same name gets this same pointer.
		asset = BinaryAsset::StoreBinaryAsset(filename,data,sz);
	}

	if (size){
		*size = asset->size;
	}
	return asset->data;
};

//The counterpart to LoadFile: it decided where the bytes came from, so it is the only thing that
//can say whether they can be let go of. See File.h for what each answer means and for the one way
//this is dangerous.
FileRelease ReleaseFile(const char* filename){
	return BinaryAsset::ReleaseBinaryAsset(filename);
}

//Deliberately shares nothing with LoadFile: no cache to consult, no cache to fill, and a missing
//file is a `false` rather than the end of the process. See File.h for when to reach for it.
bool ReadFileToString(const char* filename, std::string& out){
	out.clear();

	//Resolved the same way as LoadFile, so an app that has moved its assets under a root keeps
	//working for both kinds of read. Nothing is cached either way - that is still the difference.
	std::string resolved;
	if (!ResolveAssetPath(filename,resolved)){
		debug->Trace("ReadFileToString: could not find %s - looked in: %s\n",filename,resolved.c_str());
		return false;
	}

	FILE* file = fopen(resolved.c_str(), "rb");
	if (!file){
		debug->Trace("ReadFileToString: could not open %s\n",resolved.c_str());
		return false;
	}

	fseek(file , 0 , SEEK_END);
	long sz = ftell(file);
	rewind(file);
	if (sz < 0){
		fclose(file);
		debug->Err("ReadFileToString: could not size %s\n",filename);
		return false;
	}

	if (sz > 0){
		out.resize((size_t)sz);
		size_t got = fread(&out[0], 1, (size_t)sz, file);
		out.resize(got);     //a short read is the file's business, not a failure
	}
	fclose(file);
	return true;
}
