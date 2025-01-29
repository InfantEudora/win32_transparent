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