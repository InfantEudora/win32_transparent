#include "Texture.h"
#include "Debug.h"

static Debugger *debug = new Debugger("Texture", DEBUG_ALL);

void Texture::Create2D(int w, int h, UINT format){
    width = w;
    height = h;

    glCreateTextures(GL_TEXTURE_2D, 1, &texture_id);

    glTextureParameteri(texture_id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(texture_id, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(texture_id, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
    glTextureParameteri(texture_id, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);

    //Allocates storage in GPU, but no data is transferred
    glTextureStorage2D(texture_id, 1, format, width, height);
}