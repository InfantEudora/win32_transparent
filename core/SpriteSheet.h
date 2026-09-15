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

	// Adds a sprite covering the entire given texture, WITHOUT packing it
	// into this->texture (unlike AddSpriteFromTexture(), which copies pixel
	// data into one shared atlas) -- the sprite just points at
	// source_texture directly, same "reference, don't copy" idea as
	// AddSpritesFromGrid() below, generalized to one independent texture
	// per sprite instead of one texture sliced into many. Useful when a
	// sheet needs to hold sprites from several separately-loaded textures
	// that haven't been (or won't be) packed together -- e.g. browsing/
	// previewing a folder of source images before any packing decision has
	// been made. Ownership of source_texture stays with the caller.
	void AddSpriteFromWholeTexture(Texture* source_texture, const char* name);

	// Slices an already-loaded grid image (e.g. explosion.png, 4x4 frames)
	// into columns*rows sprites, in reading order (row 0 = top of the
	// image, left to right within a row) -- no pixel copying needed, since
	// the source image is already laid out correctly; this just computes
	// each cell's x/y/width/height/UV against atlas_texture directly.
	// atlas_texture must already be loaded (e.g. via Texture::LoadFromFile()
	// with depth_in=TEXTURE_DONT_UPLOAD -- see Upload() below for when the
	// actual GPU upload happens) -- ownership stays with the caller, unlike
	// AddSpriteFromTexture()'s internally-owned packed atlas. Sprites are
	// named "<name_prefix><index>" (e.g. "explosion_0" .. "explosion_15").
	void AddSpritesFromGrid(Texture* atlas_texture, int columns, int rows, const char* name_prefix = "");
	Sprite* GetSprite(int index);               // Return sprite by index.
	Sprite* GetLastSprite();
    Sprite* GetSprite(const char* name);        // Lookup sprite by name.

    void Upload();  // Upload to GPU

	// Drops every sprite, for reusing one SpriteSheet instance across e.g.
	// repeated folder refreshes rather than constructing a fresh one each
	// time. Does NOT touch this->texture or delete anything it points at --
	// same as ~SpriteSheet() today, texture ownership is the caller's
	// problem regardless of which Add* method populated it (see
	// AddSpriteFromWholeTexture()/AddSpritesFromGrid()'s comments).
	void Clear();

	int Count() {return sprites.size();}
};

#endif