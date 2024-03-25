#ifndef _CAMERA_H_
#define _CAMERA_H_
#include <stdint.h>
#include <string.h>
#include "Object.h"

typedef int camera_t;
#define CAMERA_TYPE_PERSPECTIVE		0
#define CAMERA_TYPE_ORTHOGRAPHIC	1

class Camera : public Object{
public:
	Camera();

	void SetType(int _type);
	void SetupPerspective(float width, float height, float fov, float znear, float zfar);
	void SetupOrthographic(float width, float height, float fov, float znear, float zfar);
	void CalculateLookatMatrix();

	camera_t 		type;		//Camera type: CAMERA_TYPE_PERSPECTIVE or CAMERA_TYPE_ORTHOGONAL

	struct {
		float	fov;
		float	zoom;
		float	znear;
		float	zfar;
		float 	width;
		float 	height;
		float 	aspect;
	}viewport;

	fmat4		mat_cam;		//Final camera matrix

private:
	//Internal intermediate variables
	fmat4		mat_frus;
	fmat4		mat_look;
};

#endif