#ifndef _SPRITESHEET_H_
#define _SPRITESHEET_H_

#include <vector>
#include "Texture.h"
#include "Sprite.h"

/*
	A spritesheet defines the positions of all spites within a single texture.
*/
class SpriteSheet{
public:
	SpriteSheet();
	~SpriteSheet();

    Texture* texture = NULL;
	std::vector<Sprite> sprites;

	void AddSpriteFromTexture(Texture* texture,  const char* name);  // Add a sprite to this sheet using the entire target texture
	Sprite* GetSprite(int index);               // Return sprite by index.
    Sprite* GetSprite(const char* name);        // Lookup sprite by name.

    void Upload();  // Upload to GPU

	int Count() {return sprites.size();}
};

#endif