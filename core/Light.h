#ifndef _LIGHT_H_
#define _LIGHT_H_

#include <stdint.h>
#include <string.h>
#include "Object.h"
#include "Camera.h"

//GLSL really wants things to be padded to 16 bytes
//light_t matches layout in shader
typedef struct {
    vec3 position = {0,0,0};
    int     shadow = 0;     // Set if the the light produces a shadow
    vec3 direction = {0,0,0};
    float   brightness = 1.0f;
    vec3 color = {1,1,1};
    float   cos_angle = 0.0f; 	// 0 means its a point light, else it becomes a cone light
}light_t;

class Light : public virtual Object{
public:
	Light(){};
	~Light(){};

	//Generic light properties
	float shadow_bias = 0.005f;
    bool f_casts_shadow = true;
    vec3 color = vec3(1,1,1);
    float brightness = 1.0f;
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