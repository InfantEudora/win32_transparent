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

		/*
		    Where this camera's viewport starts within the window, in pixels, for GetPixelRay.

		    DELIBERATELY IN CURSOR SPACE: measured from the TOP-LEFT of the window, which is what
		    GetRelativeMousePosition returns and so the space the pixel handed to GetPixelRay is
		    already in. That is NOT the convention Renderer::viewport_y uses - that one is measured
		    from the BOTTOM, because it feeds glViewport and GL's origin is down there.

		    The two are named differently (px_offset_* here, viewport_* there) precisely so the
		    mismatch cannot be mistaken for an oversight: one number talks to OpenGL, the other
		    talks to the mouse, and they are the same edge of the same rectangle measured from
		    opposite ends of the window. Renderer::DrawFrame does the flip once, next to where it
		    already writes width and height:

		        camera->viewport.px_offset_y = height - (viewport_y + GetViewportHeight());

		    Like width and height, these are overwritten every frame, so nothing needs to set them.
		    They are zero for an app that never restricts its viewport, which is all of them but
		    ApplicationTank.
		*/
		//Initialised here, unlike the fields above, because GetPixelRay may legitimately run
		//before the first frame has been drawn (the note on SetupPerspective says so) and an
		//uninitialised offset would put the ray somewhere in the next county rather than merely
		//at the wrong aspect ratio.
		float	px_offset_x = 0.0f;
		float	px_offset_y = 0.0f;
	}viewport;

	fmat4		mat_cam;		//Final camera matrix

private:
	//Internal intermediate variables
	fmat4		mat_frus;
	fmat4		mat_look;
};

//http://www.lighthouse3d.com/tutorials/view-frustum-culling/view-frustums-shape/

#endif