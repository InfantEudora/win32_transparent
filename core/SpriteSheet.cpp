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

    Sprite s;
    s.atlas = texture;
    s.width = sprite_texture->width;
    s.height = sprite_texture->height;
    s.x = texture->width;
    s.y = 0;

    //Depending on things... its gets appended
    texture->AppendTexture(sprite_texture,int2(0,0));

    sprites.push_back(s);
    //Recalculate all.
    for (Sprite& sprite:sprites){
        sprite.CalculateUV();
    }
}