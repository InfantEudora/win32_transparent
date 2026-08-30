#ifndef _HEIGHTMAP_H_
#define _HEIGHTMAP_H_

#include <vector>
#include "Mesh.h"
#include "Texture.h"


//8-bit grayscale (RGB, R=G=B) PNG heightmap, so it opens/paints normally in any image editor.
//Height values are linearly mapped to/from the byte range using min_height/max_height.
bool SaveHeightmapPNG(const char* filename, const float* heights, int width, int height, float min_height, float max_height);
bool LoadHeightmapPNG(const char* filename, std::vector<float>& out_heights, int& out_width, int& out_height, float min_height, float max_height, Texture* tex_out = NULL);

//Builds a renderable grid mesh straight from a heightmap grid - two triangles per cell,
//flat (per-triangle) normals, centered on the origin in X/Z. cell_size_x/cell_size_z are the
//world-space distance between adjacent grid points along each axis - bake your target world
//footprint in here (not via Object::SetScale afterward), since normals are computed from
//these final positions and a post-hoc non-uniform object scale won't retroactively fix them.
//Returns NULL if heights.size() != width*height.
Mesh* CreateMeshFromHeightmap(const std::vector<float>& heights, int width, int height, float cell_size_x, float cell_size_z);

#endif
