#ifndef _TEXTURE_H_
#define _TEXTURE_H_

#include <stddef.h>
#include <stdint.h>
#include <vector>
#include <string>
#include "glad.h"

class Texture{
public:
    Texture(){};
    GLuint texture_id = -1;        // OpenGL ID of the texture

    int width = 0;
    int height = 0;

    void Create2D(int width, int height, UINT format);    //Creates a 2D openGL texture
};

#endif
