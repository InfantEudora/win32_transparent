#include <cstring>
#include <string>
#include <cstdlib>
#include <math.h>
#include <iostream>
#include <algorithm>
#include <filesystem>

#include "glad.h"

#include "Shader.h"
#include "Debug.h"

static Debugger *debug = new Debugger("Shader", DEBUG_ALL);

Shader::Shader(const char* vert_path,const char* frag_path){
    debug->Info("Load and compile: %s, %s ...\n",vert_path,frag_path);

	char* vert_data = LoadFile(vert_path);
	if (!vert_data){
		return;
	}
	char* frag_data = LoadFile(frag_path);
	if (!frag_data){
		return;
	}

	debug->Info("Compiling vertex shader : %s\n", vert_path);
	vertid = CompileVertex((char*)vert_data);
	debug->Info("Compiling fragment shader : %s\n", frag_path);
	fragid = CompileFragment((char*)frag_data);
	debug->Info("Linking program\n");
	progid = LinkProgram();

	free(vert_data);
	free(frag_data);
}

Shader::~Shader(){

};

//Loads text from from file into memory.
char* Shader::LoadFile(const char* filename){
	FILE* file;
	long size = 0;

	file = fopen(filename, "rb");
	if(!file){
		debug->Fatal("LoadFile [%s] failed.\n",filename);
		return NULL;
	}

	/*get filesize:*/
	fseek(file , 0 , SEEK_END);
	size = ftell(file);
	rewind(file);

	debug->Info("LoadFile: File %s is %li bytes\n",filename,size);

	char* data = (char*)calloc(size+1,1);
	fread(data, 1, (size_t)size, file);
	fclose(file);
	return data;
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

int Shader::LinkProgram(){
	GLint result = GL_FALSE;
	int infolen = 0;

	GLuint programid = glCreateProgram();
	glAttachShader(programid, vertid);
	glAttachShader(programid, fragid);
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

	glDetachShader(programid, vertid );
	glDetachShader(programid, fragid);

	glDeleteShader(vertid);
	glDeleteShader(fragid);

	//The data may now be deleted.
	return programid;
}

void Shader::Use(){
	glUseProgram(progid);
}