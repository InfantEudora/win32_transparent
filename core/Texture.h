#ifndef _TEXTURE_H_
#define _TEXTURE_H_

#include <stddef.h>
#include <stdint.h>
#include <vector>
#include <string>
#include "glad.h"
#include "type_int2.h"
#include "type_vec3.h"
#include "RRandom.h"

#define TEXTURE_DONT_UPLOAD -1

class Texture{
public:
    Texture();
    ~Texture();
    /*
        Release the GPU copy and keep the CPU one.

        The decoded pixels (img_data) and the file bytes stay exactly where they are, so a later
        Create2D/UploadTexture pair - or ReUploadTexture, which is that pair - brings the texture
        back with no decode. What goes is the GL name and the bindless handle.

        There was no way to do this at all before, which is why Create2D could be called twice on
        one Texture and quietly ORPHAN the first name: glGenTextures hands out a fresh one and the
        old allocation stays on the GPU with nothing referring to it. Create2D calls this first now,
        so creating twice costs nothing and the second call replaces the first.

        SAFE TO CALL WHEN NOTHING IS UPLOADED. texture_id defaults to (GLuint)-1, which is the "no
        GL object" sentinel rather than 0 (glGenTextures never returns 0).

        NOT for use after a context has been destroyed - the name is already gone and deleting it
        would at best do nothing and at worst hit a DIFFERENT texture that the new context has
        since given the same number. Whoever handles a lost context should assign
        texture_id = (GLuint)-1 directly instead, which makes this and every later call a no-op
        and lets a re-upload pass tell "needs rebuilding" from "already live".
    */
    void Unload();
    GLuint texture_id = -1;         // OpenGL ID of the texture
    GLuint64 texture_handle = 0;    // OpenGL Bindless Texture Handle
    //Remembered by Create2D so ReUploadTexture can redo the same Create2D/UploadTexture pair
    //after a context recreation without the caller having to pass it again.
    GLenum gl_target = GL_TEXTURE_2D;

    int width = 0;
    int height = 0;
    int depth = 0;          // Index in a Cube Map
    GLenum storage_format = GL_RGB8;    // If 3 or 4 bytes per pixel
    GLenum image_format = GL_RGB;

    size_t file_data_sz = 0;
    uint8_t* file_data = NULL;  // Data loaded from disk

    size_t img_data_sz = 0;
    uint8_t* img_data = NULL;   // Decompressed image data (8-bit)

    size_t hdr_data_sz = 0;
    float*   hdr_data = NULL;   // Decompressed float image data (HDR)
    bool     f_is_hdr = false;

    std::string name;   //When loaded from file, it's filename.

    //Target as specified in glCreateTextures
    //target must be one of GL_TEXTURE_1D, GL_TEXTURE_2D, GL_TEXTURE_3D, GL_TEXTURE_1D_ARRAY, GL_TEXTURE_2D_ARRAY, GL_TEXTURE_RECTANGLE, GL_TEXTURE_CUBE_MAP, GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_BUFFER, GL_TEXTURE_2D_MULTISAMPLE or GL_TEXTURE_2D_MULTISAMPLE_ARRAY.

    bool IsEmpty();

    /*
        Whether the GPU copy exists RIGHT NOW.

        A different question from IsEmpty, which asks about the CPU pixels. An Unload()ed texture
        still holds every decoded byte and is one ReUploadTexture away from drawing again - what it
        does not have is a GL name, and that is the only thing a caller handing out texture units
        can act on. See Renderer::UploadMaterials, where the distinction is the difference between
        a freed unit and a unit spent binding nothing.
    */
    bool IsResident() const { return texture_id != (GLuint)-1; }

    void Create2D(int target = GL_TEXTURE_2D, int depth_in = 1);    //Creates a 2D openGL texture, but does not transfer any data

    //Allocates an empty GL_TEXTURE_3D of size^3 for something else to fill - a compute shader
    //writing into it as an image, in the one case there is so far (the cloud noise in
    //shaders/noise3d.comp). Unlike Create2D this filters LINEAR and wraps on all three axes,
    //because a volume samples it at arbitrary scales and NEAREST would show the voxel grid.
    /*
        Redoes the GPU-upload half of LoadFromMemory after a context recreation. GL objects do
        not survive the context they were made in - the same reason Mesh::ReUploadMeshData
        exists - so texture_id from a previous one is meaningless. img_data (the already-decoded
        pixels) is plain CPU memory and stays valid, so nothing is decoded twice.

        MATTERS ON ANDROID AND NOWHERE ELSE SO FAR: a desktop window keeps its GL context for
        the life of the process, while an Android app loses it on every background/foreground
        cycle. LDR GL_TEXTURE_2D only - HDR and cubemaps log and no-op rather than pretend.
    */
    void ReUploadTexture();
    //Inverts every pixel's RGB in place (255-value), leaving alpha alone, and re-uploads so the
    //GPU copy stays in step. No-op when nothing has been decoded yet.
    void InvertRGB();
    void Create3D(int size, GLenum format = GL_RGBA8);
    //The general form. The cubic one above is this with REPEAT wrapping, which is what tiling
    //noise wants; a lookup table like a cloud shadow map wants CLAMP_TO_EDGE instead, so that
    //sampling past either end returns the nearest real value rather than wrapping around to the
    //far side of the volume.
    void Create3D(int w, int h, int d, GLenum format, GLenum wrap, GLenum filter);
    void UploadTexture(GLenum _format = GL_RGB, int target = GL_TEXTURE_2D);

    void LoadCubeMapFile(const char* filename, int depth_in, Texture* first_map);
    void LoadFromMemory(uint8_t* data, size_t length, int target, int depth);
    void LoadFromFile(const char* filename, int target = GL_TEXTURE_2D, int depth_in = 0);
    void LoadHDRFromFile(const char* filename, int depth_in = TEXTURE_DONT_UPLOAD);
    //Load an image and point `rrand` at its pixels, so one decode serves both the GPU and the
    //simulation. The bytes stay this Texture's - it must outlive the generator. Pass
    //TEXTURE_DONT_UPLOAD as depth_in for CPU-side noise with no GL object. See the definition for
    //why the dependency runs this way round rather than RRandom loading its own file.
    void LoadIntoRRandomNoiseFile(const char* filename, int target = GL_TEXTURE_2D, int depth_in = 0, RRandom* rrand = NULL);

    vec3 GetValueAt(float x, float y);  // Returns pixel value [0..255] (LDR)
    vec3 GetValueAtF(float x, float y); // Returns pixel value as float (HDR), bilinear filtered

    //Compile texture
    void AppendTexture(Texture* target, int2 at);
    void AppendTexture(Texture* texture, int2 pos, int max_width);
    void CopyLine(uint8_t* line, int num_pixels, uint8_t* out, int num_color_channels);
};

#endif
