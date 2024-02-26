#include "glad.h"
#include <vector>
#include "Shader.h"
#include "Debug.h"
#include "File.h"

static Debugger *debug = new Debugger("Shader", DEBUG_ALL);

Shader::Shader(){

};

void Shader::CreateComputeShader(const char* comp_path){
	debug->Info("Load and compile: %s ...\n",comp_path);
	uint8_t* comp_data = LoadFile(comp_path,NULL);
	if (!comp_data){
		return;
	}
	int compid = -1;
	debug->Info("Compiling compute shader : %s\n", comp_path);
	compid = CompileCompute((char*)comp_data);
	debug->Info("Linking program\n");
	progid = LinkProgram(1,compid);

	free(comp_data);
};

Shader::Shader(const char* vert_path,const char* frag_path):Shader(){
    debug->Info("Load and compile: %s, %s ...\n",vert_path,frag_path);

	uint8_t* vert_data = LoadFile(vert_path,NULL);
	if (!vert_data){
		return;
	}
	uint8_t* frag_data = LoadFile(frag_path,NULL);
	if (!frag_data){
		return;
	}

	int vertid = -1;
    int fragid = -1;
	debug->Info("Compiling vertex shader : %s\n", vert_path);
	vertid = CompileVertex((char*)vert_data);
	debug->Info("Compiling fragment shader : %s\n", frag_path);
	fragid = CompileFragment((char*)frag_data);
	debug->Info("Linking program\n");
	progid = LinkProgram(2,vertid,fragid);

	free(vert_data);
	free(frag_data);
}

Shader::~Shader(){

};

int Shader::CompileVertex(char* vert_data){
	int id = glCreateShader(GL_VERTEX_SHADER);

	GLint result = GL_FALSE;
	int infolen = 0;

	glShaderSource(id, 1, (const char**)&vert_data , NULL);
	glCompileShader(id);

	glGetShaderiv(id, GL_COMPILE_STATUS, &result);
	glGetShaderiv(id, GL_INFO_LOG_LENGTH, &infolen);
	if (!result){
		char* errormsg = (char*)malloc(infolen+1);
		glGetShaderInfoLog(id, infolen, NULL, errormsg);
		debug->Fatal("CompileVertex: error: %s\n", errormsg);
		free(errormsg);
		return 0;
	}else{
		debug->Ok("Vertex shader compiled\n");
	}
	return id;
}

int Shader::CompileFragment(char* frag_data){
	int id = glCreateShader(GL_FRAGMENT_SHADER);

	GLint result = GL_FALSE;
	int infolen = 0;

	glShaderSource(id, 1, (const char**)&frag_data , NULL);
	glCompileShader(id);

	glGetShaderiv(id, GL_COMPILE_STATUS, &result);
	glGetShaderiv(id, GL_INFO_LOG_LENGTH, &infolen);
	if (!result){
		char* errormsg = (char*)malloc(infolen+1);
		glGetShaderInfoLog(id, infolen, NULL, errormsg);
		debug->Fatal("CompileFragment: error: %s\n", errormsg);
		free(errormsg);
		return 0;
	}else{
		debug->Ok("Fragment shader compiled\n");
	}
	return id;
}

int Shader::CompileCompute(char* comp_data){
	int id = glCreateShader(GL_COMPUTE_SHADER);

	GLint result = GL_FALSE;
	int infolen = 0;

	glShaderSource(id, 1, (const char**)&comp_data , NULL);
	glCompileShader(id);

	glGetShaderiv(id, GL_COMPILE_STATUS, &result);
	glGetShaderiv(id, GL_INFO_LOG_LENGTH, &infolen);
	if (!result){
		char* errormsg = (char*)malloc(infolen+1);
		glGetShaderInfoLog(id, infolen, NULL, errormsg);
		debug->Fatal("CompileFragment: error: %s\n", errormsg);
		free(errormsg);
		return 0;
	}else{
		debug->Ok("Compute shader compiled\n");
	}
	return id;
}

int Shader::LinkProgram(int count, ...){
	GLint result = GL_FALSE;
	int infolen = 0;

	GLuint programid = glCreateProgram();
	va_list arglist;
    va_start(arglist,count);
	std::vector<int>ids;
    for (int i = 0; i < count; ++i) {
        int id = va_arg(arglist, int);
		glAttachShader(programid, id);
		ids.push_back(id);
		debug->Info("LinkProgram: Attaching ID %i\n",id);
    }
    va_end(arglist);
	glLinkProgram(programid);

	// Check the program
	glGetProgramiv(programid, GL_LINK_STATUS, &result);
	glGetProgramiv(programid, GL_INFO_LOG_LENGTH, &infolen);
	if (!result){
		char* errormsg = (char*)malloc(infolen+1);
		glGetProgramInfoLog(programid, infolen, NULL, errormsg);
		debug->Fatal("LinkProgram: error: %s\n", errormsg);
		free(errormsg);
		return 0;
	}else{
		debug->Info("LinkProgram: Linked! ID: %i\n",programid);
	}

	for (int id:ids){
		glDetachShader(programid, id);
		glDeleteShader(id);
	}

	//The data may now be deleted.
	return programid;
}

void Shader::Use(){
	glUseProgram(progid);
}

//Set a uniform int
void Shader::Setint(const char* name, int value){
	GLuint intid = glGetUniformLocation(progid, name);
	if (intid == -1){
		//Warn once.
		debug->Fatal("Could not set %i's int %s\n",progid,name);
		return;
	}
	glUniform1i(intid,(GLint)value);
}

void Shader::Setmat3(const char* name, const fmat3& matrix){
	GLuint matid = glGetUniformLocation(progid, name);
	if (matid == -1){
		debug->Fatal("Could not set %i's mat4 %s\n",progid,name);
		return;
	}
	glUniformMatrix3fv(matid,1,GL_FALSE,(GLfloat*)&matrix);
}

void Shader::Setmat4(const char* name, const fmat4& matrix){
	GLuint matid = glGetUniformLocation(progid, name);
	if (matid == -1){
		debug->Fatal("Could not set %i's mat4 %s\n",progid,name);
		return;
	}
	glUniformMatrix4fv(matid,1,GL_FALSE,(GLfloat*)&matrix);
}