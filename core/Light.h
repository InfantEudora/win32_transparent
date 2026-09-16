#ifndef _LIGHT_H_
#define _LIGHT_H_

#include <stdint.h>
#include <string.h>
#include "Object.h"
#include "Camera.h"

//GLSL really wants things to be padded to 16 bytes
//light_t matches layout in shader
//Named rather than typedef'd, for the reason Material.h records above its own struct: the default
//member initialisers make this non-C-compatible, so it cannot borrow a linkage name from a typedef.
struct light_t {
    vec3 position = {0,0,0};
    int     shadow = 0;     // Set if the the light produces a shadow
    vec3 direction = {0,0,0};
    float   brightness = 1.0f;
    vec3 color = {1,1,1};
    float   cos_angle = 0.0f; 	// 0 means its a point light, else it becomes a cone light
    /*
        How big the lamp is, in world units, for the penumbra estimate - 0 is a point source and a
        hard edge. Resolved by Renderer::UploadLights, so what arrives here is always a real
        radius and never the "ask the renderer" sentinel that Light::radius may hold.

        APPENDED LAST, AND THE NEXT ONE MUST BE TOO. Everything above it keeps the offset it has
        always had, so a shader that has not been updated still reads position, direction, colour
        and cos_angle correctly - it simply does not see this. The same care Material.h documents
        for its own struct, and for the same reason: THIS LAYOUT IS WRITTEN OUT BY HAND IN EVERY
        SHADER THAT READS A LIGHT, and there is no compiler to catch a copy that drifted.

        The three floats of padding are not decoration. Each vec3 above is followed by a scalar so
        that it exactly fills a 16-byte slot, which is what lets this struct match std430 with no
        alignment attributes on either side; this field needs a fourth slot and has to fill it.

        The copies, as of 2026-09-12:
            shared_assets/shaders/default.frag      (the one that uses it, in CalcFieldShadow)
            apps/ship/assets/shaders/raymarch_volume.frag
            shaders/breakout_shield.frag
            shaders/custom.frag
    */
    float   radius = 0.0f;
    float   pad0 = 0.0f;
    float   pad1 = 0.0f;
    float   pad2 = 0.0f;
};

class Light : public virtual Object{
public:
	Light(){};
	~Light(){};

	//Generic light properties
	float shadow_bias = 0.005f;
    bool f_casts_shadow = true;
    vec3 color = vec3(1,1,1);
    float brightness = 1.0f;

    /*
        How big this lamp is, in world units. It sets how fast its shadow edges soften with
        distance from whatever cast them: 0 is a point source and a hard edge, and bigger is
        softer. Read by the occluder field's march (CalcFieldShadow in default.frag), which is
        what shadows point and cone lights.

        NEGATIVE MEANS "use Renderer::field_light_radius", the scene-wide default, and that is
        what a light starts at - so every existing scene keeps the single global softness it was
        tuned with, and the Engine panel's slider still moves all of them at once. Set it to give
        one lamp an edge of its own. A negative radius is not a value anything could legitimately
        want, which is what makes it safe to use as the sentinel here.
    */
    float radius = -1.0f;
};

//Light used for the sun
class DirectionalLight: public Light, public Camera{
public:
	DirectionalLight();
	~DirectionalLight(){};
};

//Search lights and such. Defined with a direction vector and an arc between 0 and 180 degrees
class ConeLight: public Light{
public:
	ConeLight();
	~ConeLight(){};
	float cone_angle = 90.0f;
};

class PointLight: public Light{
public:
	PointLight();
	~PointLight(){};
};


#endif