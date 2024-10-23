#include "Light.h"
#include "Debug.h"

static Debugger* debug = new Debugger("Light",DEBUG_ALL);

DirectionalLight::DirectionalLight(){
	SetupOrthographic(1024,1024,60.0f,0.1,100.0f);
	debug->Info("Created directional light.\n");
}

ConeLight::ConeLight(){
	debug->Info("Created Cone Light.\n");
}

PointLight::PointLight(){
    f_casts_shadow = false;
	debug->Info("Created PointLight.\n");
}