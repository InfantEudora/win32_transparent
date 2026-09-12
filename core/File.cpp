#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "File.h"

#include "Debug.h"
static Debugger *debug = new Debugger("File", DEBUG_ALL);

std::string GetBasePath(const char* filename){
	std::string sname = filename;
  	std::size_t found = sname.find_last_of("/\\");
    if (found == -1){
        return "";
    }
  	return sname.substr(0,found);
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
	BinaryAsset* asset = BinaryAsset::GetBinaryAsset(filename);
	if (!asset){
		FILE* file = fopen(filename, "rb");
		if(!file){
			debug->Fatal("LoadFile failed to load: [%s]\n",filename);
			return NULL;
		}

		/*get filesize:*/
		fseek(file , 0 , SEEK_END);
		size_t sz = ftell(file);
		rewind(file);

		debug->Info("LoadFile: File %s is %li bytes\n",filename,sz);

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

	FILE* file = fopen(filename, "rb");
	if (!file){
		debug->Trace("ReadFileToString: could not open %s\n",filename);
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
