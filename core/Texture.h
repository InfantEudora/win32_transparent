#ifndef _TEXTURE_H_
#define _TEXTURE_H_

#include <stddef.h>
#include <stdint.h>
#include <vector>
#include <string>
#include "glad.h"
#include "type_int2.h"
#include "type_vec3.h"

#define TEXTURE_DONT_UPLOAD -1

class Texture{
public:
    Texture();
    ~Texture();
    GLuint texture_id = -1;         // OpenGL ID of the texture
    GLuint64 texture_handle = 0;    // OpenGL Bindless Texture Handle

    int width = 0;
    int height = 0;
    int depth = 0;          // Index in a Cube Map
    UINT storage_format = GL_RGB8;    // If 3 or 4 bytes per pixel
    UINT image_format = GL_RGB;

    size_t file_data_sz = 0;
    uint8_t* file_data = NULL;  // Data loaded from disk

    size_t img_data_sz = 0;
    uint8_t* img_data = NULL;   // Decompressed image data

    std::string name;   //When loaded from file, it's filename.

    //Target as specified in glCreateTextures
    //target must be one of GL_TEXTURE_1D, GL_TEXTURE_2D, GL_TEXTURE_3D, GL_TEXTURE_1D_ARRAY, GL_TEXTURE_2D_ARRAY, GL_TEXTURE_RECTANGLE, GL_TEXTURE_CUBE_MAP, GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_BUFFER, GL_TEXTURE_2D_MULTISAMPLE or GL_TEXTURE_2D_MULTISAMPLE_ARRAY.

    bool IsEmpty();

    void Create2D(int target = GL_TEXTURE_2D, int depth_in = 1);    //Creates a 2D openGL texture, but does not transfer any data
    void UploadTexture(UINT _format = GL_RGB, int target = GL_TEXTURE_2D);

    void LoadCubeMapFile(const char* filename, int depth_in, Texture* first_map);
    void LoadFromMemory(uint8_t* data, size_t length, int target, int depth);
    void LoadFromFile(const char* filename, int target = GL_TEXTURE_2D, int depth_in = 0);

    vec3 GetValueAt(float x, float y); //Returns the pixel value at 0 ... 1 interval.

    //Compile texture
    void AppendTexture(Texture* target, int2 at);
    void AppendTexture(Texture* texture, int2 pos, int max_width);
    void CopyLine(uint8_t* line, int num_pixels, uint8_t* out, int num_color_channels);
};

#endif
