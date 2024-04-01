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
	SetType(CAMERA_TYPE_PERSPECTIVE);
	CalculateLookatMatrix();
}

void Camera::SetupOrthographic(float _width, float _height, float _fov, float _znear, float _zfar){
	viewport.width 	= _width;
	viewport.height = _height;
	viewport.fov 	= _fov;
	viewport.zoom 	= 40;
	viewport.znear	= _znear;
	viewport.zfar	= _zfar;
	SetType(CAMERA_TYPE_ORTHOGRAPHIC);
	CalculateLookatMatrix();
}

void Camera::SetType(int _type){
	type = _type;
	//Update the camera
	if (type == CAMERA_TYPE_ORTHOGRAPHIC){
		//mat_frus = matrix_ortho(-viewport.width/2,viewport.width/2,-viewport.height/2,viewport.height/2,viewport.znear,viewport.zfar);
	}else{
		mat_frus.perspectivematrix(viewport.fov,viewport.aspect,viewport.znear, viewport.zfar);
	}
}

//Update lookat. Called before rendering a new frame.
void Camera::CalculateLookatMatrix(){
	//Recalculate all the things.
	viewport.aspect = viewport.width / viewport.height;
	if (type == CAMERA_TYPE_ORTHOGRAPHIC){
		//mfrus = matrix_ortho(-viewport.zoom*viewport.aspect,viewport.zoom*viewport.aspect,-viewport.zoom,viewport.zoom,viewport.znear,viewport.zfar);
	}else{
		mat_frus.perspectivematrix(viewport.fov,viewport.aspect, viewport.znear, viewport.zfar);
	}
	mat_look.lookatmatrix(state.position,state.position - (state.rotation * ref_forward),state.rotation * ref_up);
	mat_cam = mat_look * mat_frus;
}
