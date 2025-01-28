#ifndef _SPRITE_H_
#define _SPRITE_H_

#include "Texture.h"
#include "type_vec2.h"
/*
	A sprite is a single image located in a part of a texture.
*/
class Sprite{
public:
	Sprite(){};
	~Sprite(){};
	std::string name;			// If you want to look it up by name

    Texture* atlas = NULL;      //The texture where it's in.

	int width = 0;         // Pixel width within texture
	int height = 0;        // Pixel height within texture
	int x = 0;	            // Top left corner location in texture
	int y = 0;
	//TextureCoordinates (0-1 range in this texture) can be converted to width - height
	vec2 uv0 = {0};
	vec2 uv1 = {0};

	void CalculateUV();
};

#endif