#include "glad.h"
#include <vector>
#include "Shader.h"
#include "Debug.h"
#include "File.h"
#include <string>

static Debugger *debug = new Debugger("Shader", DEBUG_INFO);

//Everything up to the last separator, or "" for a bare filename.
static std::string DirectoryOf(const std::string& path){
	size_t slash = path.find_last_of("/\\");
	if (slash == std::string::npos){
		return std::string();
	}
	return path.substr(0,slash + 1);
}

/*
	A minimal #include for GLSL.

	Neither GL nor this loader has one, so any function shared between two shaders has to be
	duplicated - which is exactly how a density function and the shadow map that is supposed to
	match it drift apart without anything looking broken. shaders/density.glsl is shared between
	raymarch_volume.frag and cloud_shadow.comp for that reason.

	Only a line whose first non-space token is #include followed by a quoted path is touched, and
	the path resolves relative to the INCLUDING file's directory. Nested includes work; a cycle is
	cut off by the depth limit rather than detected properly, because the only thing that makes
	one is a typo.

	An included file must NOT carry its own #version - that has to stay the first line of the
	file doing the including.

	Line numbers: each spliced chunk is wrapped in `#line 1 <n>` and the return to the outer file
	is `#line <next> 0`, so a compile error inside an include reports as <n>(line) and errors in
	the main file keep their real numbers. Without it every error after the first include points
	somewhere misleading, which is worse than having no includes at all.
*/
static std::string ResolveIncludes(const std::string& path, const char* data, size_t size,
								   int depth, int* source_index){
	if (depth > 8){
		debug->Err("Shader include nesting too deep at %s - a cycle?\n",path.c_str());
		return std::string(data,size);
	}
	std::string dir = DirectoryOf(path);
	std::string src(data,size);
	std::string out;
	out.reserve(src.size());

	size_t pos = 0;
	int line_number = 1;
	while (pos <= src.size()){
		size_t eol = src.find('\n',pos);
		size_t end = (eol == std::string::npos) ? src.size() : eol;
		std::string line = src.substr(pos,end - pos);

		size_t first = line.find_first_not_of(" \t");
		bool handled = false;
		if ((first != std::string::npos) && (line.compare(first,8,"#include") == 0)){
			size_t q1 = line.find('"',first + 8);
			size_t q2 = (q1 == std::string::npos) ? std::string::npos : line.find('"',q1 + 1);
			if ((q1 != std::string::npos) && (q2 != std::string::npos)){
				std::string name = line.substr(q1 + 1,q2 - q1 - 1);
				std::string full = dir + name;
				size_t inc_sz = 0;
				uint8_t* inc_data = LoadFile(full.c_str(),&inc_sz);
				if (!inc_data){
					//LoadFile has already named the file. Leaving the directive in would only add
					//a second, more confusing error from the GLSL compiler on top of this one.
					debug->Err("Shader %s includes %s, which could not be read\n",
							   path.c_str(),full.c_str());
					return out + src.substr(pos);
				}
				int my_index = ++(*source_index);
				out += "#line 1 " + std::to_string(my_index) + "\n";
				out += ResolveIncludes(full,(const char*)inc_data,inc_sz,depth + 1,source_index);
				out += "\n#line " + std::to_string(line_number + 1) + " 0\n";
				handled = true;
			}
		}
		if (!handled){
			out += line;
			out += '\n';
		}

		if (eol == std::string::npos){
			break;
		}
		pos = eol + 1;
		line_number++;
	}
	return out;
}

//Reads a shader file and splices in whatever it #includes. Empty if the file could not be read.
static std::string LoadShaderSource(const char* path){
	size_t sz = 0;
	uint8_t* data = LoadFile(path,&sz);
	if (!data){
		return std::string();
	}
	int source_index = 0;
	return ResolveIncludes(path,(const char*)data,sz,0,&source_index);
}

Shader::Shader(){

};

void Shader::CreateComputeShader(const char* comp_path){
	debug->Info("Load and compile: %s ...\n",comp_path);
	std::string comp_src = LoadShaderSource(comp_path);
	if (comp_src.empty()){
		return;
	}
	int compid = -1;
	debug->Info("Compiling compute shader : %s\n", comp_path);
	compid = CompileCompute((char*)comp_src.c_str(),comp_src.size());
	debug->Info("Linking program\n");
	progid = LinkProgram(1,compid);

	//TODO: Figure out if loaded from file or from memory.
	//free(comp_data);
};

Shader::Shader(const char* vert_path,const char* frag_path):Shader(){
    debug->Info("Load and compile: %s, %s ...\n",vert_path,frag_path);

	std::string vert_src = LoadShaderSource(vert_path);
	if (vert_src.empty()){
		return;
	}
	std::string frag_src = LoadShaderSource(frag_path);
	if (frag_src.empty()){
		return;
	}

	int vertid = -1;
    int fragid = -1;
	debug->Info("Compiling vertex shader : %s\n", vert_path);
	vertid = CompileVertex((char*)vert_src.c_str(),vert_src.size());
	debug->Info("Compiling fragment shader : %s\n", frag_path);
	fragid = CompileFragment((char*)frag_src.c_str(),frag_src.size());
	debug->Info("Linking program\n");
	progid = LinkProgram(2,vertid,fragid);
	vname = vert_path;
	fname = frag_path;


	//TODO: Figure out if loaded from file or from memory.
	//free(vert_data);
	//free(frag_data);
}

Shader::~Shader(){

};

int Shader::CompileVertex(char* vert_data, size_t size){
	int id = glCreateShader(GL_VERTEX_SHADER);

	GLint result = GL_FALSE;
	int infolen = 0;
	GLint sz = size;

	glShaderSource(id, 1, (const char**)&vert_data , &sz);
	glCompileShader(id);

	glGetShaderiv(id, GL_COMPILE_STATUS, &result);
	glGetShaderiv(id, GL_INFO_LOG_LENGTH, &infolen);
	if (!result){
		char* errormsg = (char*)malloc(infolen+1);
		glGetShaderInfoLog(id, infolen, NULL, errormsg);
		debug->Fatal("CompileVertex: error: %s\n", errormsg);
		free(errormsg);
		return 0;
	}
	if (infolen > 1){
		char* errormsg = (char*)malloc(infolen+1);
		glGetShaderInfoLog(id, infolen, NULL, errormsg);
		debug->Warn("CompileVertex: warning: %s\n", errormsg);
		free(errormsg);
	}else{
		debug->Ok("Vertex shader compiled\n");
	}
	return id;
}

int Shader::CompileFragment(char* frag_data, size_t size){
	int id = glCreateShader(GL_FRAGMENT_SHADER);

	GLint result = GL_FALSE;
	int infolen = 0;
	GLint sz = size;

	glShaderSource(id, 1, (const char**)&frag_data , &sz);
	glCompileShader(id);

	glGetShaderiv(id, GL_COMPILE_STATUS, &result);
	glGetShaderiv(id, GL_INFO_LOG_LENGTH, &infolen);
	if (!result){
		char* errormsg = (char*)malloc(infolen+1);
		glGetShaderInfoLog(id, infolen, NULL, errormsg);
		debug->Fatal("CompileFragment: error: %s\n", errormsg);
		free(errormsg);
		return 0;
	}
	if (infolen > 1){
		char* errormsg = (char*)malloc(infolen+1);
		glGetShaderInfoLog(id, infolen, NULL, errormsg);
		debug->Warn("CompileFragment: warning: %s\n", errormsg);
		free(errormsg);
	}else{
		debug->Ok("Fragment shader compiled\n");
	}
	return id;
}

int Shader::CompileCompute(char* comp_data, size_t size){
	int id = glCreateShader(GL_COMPUTE_SHADER);

	GLint result = GL_FALSE;
	int infolen = 0;
	GLint sz = size;

	glShaderSource(id, 1, (const char**)&comp_data , &sz);
	glCompileShader(id);

	glGetShaderiv(id, GL_COMPILE_STATUS, &result);
	glGetShaderiv(id, GL_INFO_LOG_LENGTH, &infolen);
	if (!result){
		char* errormsg = (char*)malloc(infolen+1);
		glGetShaderInfoLog(id, infolen, NULL, errormsg);
		debug->Fatal("CompileCompute: error: %s\n", errormsg);
		free(errormsg);
		return 0;
	}
	if (infolen > 1){
		char* errormsg = (char*)malloc(infolen+1);
		glGetShaderInfoLog(id, infolen, NULL, errormsg);
		debug->Warn("CompileCompute: warning: %s\n", errormsg);
		free(errormsg);
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
	}
	if (infolen > 1){
		char* errormsg = (char*)malloc(infolen+1);
		glGetProgramInfoLog(programid, infolen, NULL, errormsg);
		debug->Warn("LinkProgram: warning: %s\n", errormsg);
		free(errormsg);
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

/*
    The uniform setters all name their own program (glProgramUniform*) rather than writing to
    whichever one is bound (glUniform*). That is not a style preference: with glUniform, asking
    shader A to set a uniform while shader B is bound silently writes it into B - the location
    was looked up in A, so it lands on whatever uniform happens to live at that index in B. It
    fails without any error, and the result can look plausible: inserting a pass that bound a
    different program ahead of one such call left the entire colour pass rendering from the sun's
    shadow matrix, which read as an odd camera angle rather than as a bug.

    Two consequences worth knowing. Uniform sets no longer care about bind order, so Use() is
    only needed before drawing. And any call site that was previously relying on the old
    behaviour - naming one Shader while meaning the bound one - is now actually broken and has to
    be corrected; see RenderDepthPasses(skinned_shader,...) in Renderer::DrawFrame.
*/
//Set a uniform int
bool Shader::Setint(const char* name, int value){
	GLuint intid = glGetUniformLocation(progid, name);
	if (intid == -1){
		//Warn once.
		debug->Err("Could not set %i's int %s\n",progid,name);
		return false;
	}
	glProgramUniform1i(progid,intid,(GLint)value);
	return true;
}

void Shader::Setfloat(const char* name, const float& value){
	GLuint fid = glGetUniformLocation(progid, name);
	if (fid == -1){
		debug->Warn("Could not set %i's vec3 %s\n",progid,name);
		return;
	}else{
		//debug->Info("Uniform %s at location %i\n",name,fid);
	}
	glProgramUniform1fv(progid,fid,1,(GLfloat*)&value);

}

void Shader::Setvec3(const char* name, const vec3& value){
	GLuint fid = glGetUniformLocation(progid, name);
	if (fid == -1){
		debug->Warn("Could not set %i's vec3 %s\n",progid,name);
		return;
	}else{
		//debug->Info("Uniform %s at location %i\n",name,fid);
	}
	glProgramUniform3fv(progid,fid,1,(const GLfloat*)&value);
	//glUniform3f(fid,value.x,value.y,value.z);
}

void Shader::Setmat3(const char* name, const fmat3& matrix){
	GLuint matid = glGetUniformLocation(progid, name);
	if (matid == -1){
		debug->Fatal("Could not set %i's mat4 %s\n",progid,name);
		return;
	}
	glProgramUniformMatrix3fv(progid,matid,1,GL_FALSE,(GLfloat*)&matrix);
}

void Shader::Setmat4(const char* name, const fmat4& matrix){
	GLuint matid = glGetUniformLocation(progid, name);
	if (matid == -1){
		debug->Fatal("Could not set %i's mat4 %s\n",progid,name);
		return;
	}
	glProgramUniformMatrix4fv(progid,matid,1,GL_FALSE,(GLfloat*)&matrix);
}