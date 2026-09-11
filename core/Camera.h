#ifndef _CAMERA_H_
#define _CAMERA_H_
#include <stdint.h>
#include <string.h>
#include "Object.h"
#include "type_ray.h"
#include "type_int2.h"

typedef int camera_t;
#define CAMERA_TYPE_PERSPECTIVE		0
#define CAMERA_TYPE_ORTHOGRAPHIC	1

class Camera : public virtual Object{
public:
	Camera();

	void SetType(int _type);

	/*
	    NOTE ON width AND height, for both of these: they are overwritten every frame.

	    Renderer::DrawFrame assigns camera->viewport.width/height from the renderer's viewport
	    (GetViewportWidth()/GetViewportHeight()) and calls CalculateLookatMatrix() before it draws
	    anything, so whatever is passed here survives only until the first frame. That is the
	    behaviour you want - it is what makes the camera track a window resize on its own, and no
	    caller has to do anything about it - but it does mean the two arguments the signature leads
	    with are not a setting. They only matter if something reads viewport.aspect BEFORE the
	    first frame has been drawn (GetPixelRay does), so passing the real dimensions is still the
	    right thing to do; just do not expect to control the aspect ratio with them.

	    The arguments are kept rather than removed because they make the call self-documenting at
	    the twenty-odd sites that already pass renderer->width/height.
	*/
	void SetupPerspective(float width, float height, float fov, float znear, float zfar);
	void SetupOrthographic(float width, float height, float zoom, float znear, float zfar);
	void CalculateLookatMatrix();
	fmat4 GetPositionlessMatrix();

	ray  GetRay();
	ray  GetPixelRay(int2& px_coord);

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

//http://www.lighthouse3d.com/tutorials/view-frustum-culling/view-frustums-shape/

#endif