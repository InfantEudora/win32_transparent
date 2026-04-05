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

    // Set when built via LoadFromEquirectangular — single GL cubemap object.
    GLuint cubemap_id = 0;

    void LoadFromFile(const char* filename, int depth_in);

    // Convert an equirectangular image (HDR or LDR) to a cubemap.
    // face_size: pixel size of each face. 0 = auto (source height / 2).
    void LoadFromEquirectangular(const char* filename, int face_size = 0);
};

#endif
