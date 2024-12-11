#include "CubeMap.h"
#include "Debug.h"
#include "File.h"

static Debugger *debug = new Debugger("CubeMap", DEBUG_ALL);

//Uses the first cubemap file to create an image. The second one uses the main image handle.
void CubeMap::LoadFromFile(const char* filename, int depth_in){
    if ((depth_in < 0) || (depth_in > 5)){
        debug->Fatal("Invalid CubeMap index\n");
    }
    texture[depth_in] = new Texture();
    texture[depth_in]->LoadCubeMapFile(filename,depth_in,texture[0]);
}