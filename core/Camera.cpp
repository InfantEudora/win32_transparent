#include "Camera.h"
#include "Debug.h"

static Debugger* debug = new Debugger("Camera",DEBUG_ALL);

//Emptry constructor
Camera::Camera():Object(){
	//Camera should start with a position, so that all subsequent operations on it's
	//transformation make sense.
};

void Camera::SetupPerspective(float _width, float _height, float _fov, float _znear, float _zfar){
	viewport.width 	= _width;
	viewport.height = _height;
	viewport.fov 	= _fov;
	viewport.zoom 	= 10;
	viewport.znear	= _znear;
	viewport.zfar	= _zfar;
	type = CAMERA_TYPE_PERSPECTIVE;
	CalculateLookatMatrix();
}

void Camera::SetupOrthographic(float _width, float _height, float _zoom, float _znear, float _zfar){
	viewport.width 	= _width;
	viewport.height = _height;
	viewport.fov 	= 90;
	viewport.zoom 	= _zoom;
	viewport.znear	= _znear;
	viewport.zfar	= _zfar;
	type = CAMERA_TYPE_ORTHOGRAPHIC;
	CalculateLookatMatrix();
}

void Camera::SetType(int _type){
	type = _type;
	//Update the camera
	CalculateLookatMatrix();
}

//Returns the lookat without position (for skybox)
fmat4 Camera::GetPositionlessMatrix(){
	fmat4 m;

	viewport.aspect = viewport.width / viewport.height;
	if (type == CAMERA_TYPE_ORTHOGRAPHIC){
		//mfrus = matrix_ortho(-viewport.zoom*viewport.aspect,viewport.zoom*viewport.aspect,-viewport.zoom,viewport.zoom,viewport.znear,viewport.zfar);
	}else{
		mat_frus.perspectivematrix(viewport.fov,viewport.aspect, viewport.znear, viewport.zfar);
	}
	m.lookatmatrix(vec3(),state.rotation * ref_forward,state.rotation * ref_up);
	m = m * mat_frus;
	return m;
}

//Update lookat. Called before rendering a new frame.
void Camera::CalculateLookatMatrix(){
	//Recalculate all the things.
	viewport.aspect = viewport.width / viewport.height;
	if (type == CAMERA_TYPE_ORTHOGRAPHIC){
		mat_frus.orthographic_matrix(-viewport.zoom*viewport.aspect,viewport.zoom*viewport.aspect,-viewport.zoom,viewport.zoom,viewport.znear,viewport.zfar);
	}else{
		mat_frus.perspectivematrix(viewport.fov,viewport.aspect, viewport.znear, viewport.zfar);
	}
	mat_look.lookatmatrix(state.position,state.position + (state.rotation * ref_forward),state.rotation * ref_up);
	mat_cam = mat_look * mat_frus;
}

//Returns a ray from the center of the camera.
ray Camera::GetRay(){
	ray r;
	r.origin = GetPosition();
	r.direction = GetForward();
	return r;
}
//Returns a ray from the center of the camera at pixel posiion
//TODO: Fix this for Orthographic projection
ray Camera::GetPixelRay(int2& px_coord){
	ray r;
	//The end of the ray is on the far clipping plane
	//Convert the pixel coord to a -0.5/0.5 space
	float w = (px_coord.x - (viewport.width * 0.5)) / (viewport.width * 0.5);
	float h = (viewport.height-px_coord.y - (viewport.height * 0.5)) / (viewport.height * 0.5);

	//Compute the near and far plane dimensions
	float hfar = 2 * tan(toradians(viewport.fov) / 2) * viewport.zfar;
	float wfar = hfar * viewport.aspect;

	vec3 far_center = GetPosition() + (GetForward() * viewport.zfar);
	vec3 far_point = far_center + (GetUp() * h * (hfar / 2.0)) + (GetLeft() * w * (wfar/2.0));

	r.origin = GetPosition();
	r.direction = (far_point - r.origin).normalize();
	return r;
}
