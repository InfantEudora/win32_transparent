#include "SpriteSheet.h"
#include "Debug.h"

static Debugger *debug = new Debugger("SpriteSheet", DEBUG_ALL);

SpriteSheet::SpriteSheet(){

}

SpriteSheet::~SpriteSheet(){

}

Sprite* SpriteSheet::GetSprite(int index){
	if (sprites.size() > index){
		return &sprites.at(index);
	}
	return NULL;
}

Sprite* SpriteSheet::GetLastSprite(){
	if (!sprites.empty()){
		return &sprites.back();
	}
	return NULL;
}

void SpriteSheet::AddSpritesFromGrid(Texture* atlas_texture, int columns, int rows, const char* name_prefix){
	if (!atlas_texture){
		debug->Err("AddSpritesFromGrid: no atlas texture given\n");
		return;
	}
	if ((columns <= 0) || (rows <= 0)){
		debug->Err("AddSpritesFromGrid: columns/rows must be positive (got %d x %d)\n", columns, rows);
		return;
	}
	if (atlas_texture->IsEmpty()){
		debug->Err("AddSpritesFromGrid: %s has no decoded image data -- load it first\n", atlas_texture->name.c_str());
		return;
	}
	if (texture && (texture != atlas_texture)){
		debug->Warn("AddSpritesFromGrid: this SpriteSheet already has a different texture (%s) -- overwriting with %s\n",
			texture->name.c_str(), atlas_texture->name.c_str());
	}
	if (((atlas_texture->width % columns) != 0) || ((atlas_texture->height % rows) != 0)){
		debug->Warn("AddSpritesFromGrid: %s (%d x %d) doesn't divide evenly into %d x %d cells\n",
			atlas_texture->name.c_str(), atlas_texture->width, atlas_texture->height, columns, rows);
	}

	texture = atlas_texture;
	int cell_width = atlas_texture->width / columns;
	int cell_height = atlas_texture->height / rows;

	for (int row = 0; row < rows; row++){
		for (int col = 0; col < columns; col++){
			Sprite s;
			s.atlas = texture;
			s.width = cell_width;
			s.height = cell_height;
			s.x = col * cell_width;
			s.y = row * cell_height;
			s.name = std::string(name_prefix) + std::to_string(row * columns + col);
			s.CalculateUV();
			sprites.push_back(s);
		}
	}
	debug->Info("AddSpritesFromGrid: added %d sprites (%d x %d cells, %d x %d px each) from %s\n",
		columns * rows, columns, rows, cell_width, cell_height, atlas_texture->name.c_str());
}

void SpriteSheet::AddSpriteFromWholeTexture(Texture* source_texture, const char* name){
	if (!source_texture){
		debug->Err("AddSpriteFromWholeTexture: no texture given\n");
		return;
	}
	if (source_texture->IsEmpty()){
		debug->Err("AddSpriteFromWholeTexture: %s has no decoded image data -- load it first\n", source_texture->name.c_str());
		return;
	}

	Sprite s;
	s.atlas = source_texture;
	s.name = name;
	s.width = source_texture->width;
	s.height = source_texture->height;
	s.x = 0;
	s.y = 0;
	s.uv0 = vec2(0.0f, 0.0f);
	s.uv1 = vec2(1.0f, 1.0f);
	sprites.push_back(s);
}

void SpriteSheet::Clear(){
	sprites.clear();
}

void SpriteSheet::Upload(){
    if (texture){
        texture->Create2D();
        texture->UploadTexture();
    }
}

void SpriteSheet::AddSpriteFromTexture(Texture* sprite_texture,  const char* name){
    //If we don't have a texture, create an empty one.
    if (!texture){
        texture = new Texture();
    }

    //Because of lazyness... we only allow for sprites of the same size to be added.
    if (sprites.size() > 0){
        if ((sprites.at(0).width != sprite_texture->width) || (sprites.at(0).height != sprite_texture->height)){
            debug->Fatal("AddSpriteFromTexture: Adding sprite's with different sizes/widths is super extra not supported.\n");
        }
    }

    //We'd like to place this sprite after the last sprite
    int2 at = int2(0,0);
    Sprite* last_sprite = GetLastSprite();
    int max_num_horizontal_sprites = 3;
    if (last_sprite){
        int x = last_sprite->x + last_sprite->width;

        if (x >= (max_num_horizontal_sprites * sprite_texture->width)) {
            //Start at a new line
            at.y = last_sprite->y + last_sprite->height;
            at.x = 0;
        }else{
            at.y = last_sprite->y;
            at.x = x;
        }
    }

    //Build the sprite
    Sprite s;
    s.atlas = texture;
    s.width = sprite_texture->width;
    s.height = sprite_texture->height;
    s.x = at.x;
    s.y = at.y;

    texture->AppendTexture(sprite_texture,at);

    sprites.push_back(s);
    //Recalculate all.
    for (Sprite& sprite:sprites){
        sprite.CalculateUV();
    }
}