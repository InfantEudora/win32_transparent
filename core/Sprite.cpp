#include "Sprite.h"
#include "Debug.h"

static Debugger *debug = new Debugger("Sprite", DEBUG_ALL);

//Calculate UV coordinates from pixel dimensions.
/* UVs are
 0,1		1,1

 0,0		1,0

 UV0 is bottom left
 UV1 is top right

 Sprite coordinates are x,y is top left in sprite
*/
void Sprite::CalculateUV(){
	if (!atlas){
		debug->Warn("Sprite has no Atlas to calculate UV on\n");
		return;
	}
	//The size of the sprite in 0 ... 1 uv range
	double dx = (1.0 / (double)atlas->width);
	double dy = (1.0 / (double)atlas->height);

	uv0.x = dx * (double)x;
	uv0.y =(dy * (double)y);
	uv1.x = (dx * (double)x) + (dx * (double)width);
	uv1.y = (dy * (double)y) + (dy * (double)height);

	//debug->Trace("Atlas w x h = %llu x %llu\n",atlas->width,atlas->height);
	//debug->Trace("Sprite w x h = %llu x %llu\n",width,height);
	//debug->Trace("UV0: %.3f %.3f UV1 %.3f %.3f\n",uv0.x,uv0.y,uv1.x,uv1.y);
}

// Inverse of CalculateUV() -- recomputes x/y/width/height from the current
// uv0/uv1, so the two representations don't drift apart after something
// (e.g. a UV editor) sets uv0/uv1 directly instead of going through pixel
// coordinates.
void Sprite::CalculatePixelRect(){
	if (!atlas){
		debug->Warn("Sprite has no Atlas to calculate pixel rect on\n");
		return;
	}
	x = (int)(uv0.x * (double)atlas->width);
	y = (int)(uv0.y * (double)atlas->height);
	width  = (int)((uv1.x - uv0.x) * (double)atlas->width);
	height = (int)((uv1.y - uv0.y) * (double)atlas->height);
}
