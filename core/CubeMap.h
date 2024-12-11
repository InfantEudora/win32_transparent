#ifndef _TEXTURE_CUBEMAP_H_
#define _TEXTURE_CUBEMAP_H_

#include <stddef.h>
#include <stdint.h>
#include <vector>
#include <string>
#include "glad.h"

#include "Texture.h"

class CubeMap{
public:
    CubeMap(){
        texture[0] = NULL;
        texture[1] = NULL;
        texture[2] = NULL;
        texture[3] = NULL;
        texture[4] = NULL;
        texture[5] = NULL;
    };
    Texture* texture[6];

    void LoadFromFile(const char* filename, int depth_in);
};

#endif
