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
								   int depth, int* source_index,
								   std::vector<std::string>* files_used){
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
				if (files_used){
					files_used->push_back(full);
				}
				int my_index = ++(*source_index);
				out += "#line 1 " + std::to_string(my_index) + "\n";
				out += ResolveIncludes(full,(const char*)inc_data,inc_sz,depth + 1,source_index,files_used);
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
static std::string LoadShaderSource(const char* path, std::vector<std::string>* files_used){
	size_t sz = 0;
	uint8_t* data = LoadFile(path,&sz);
	if (!data){
		return std::string();
	}
	if (files_used){
		files_used->push_back(path);
	}
	int source_index = 0;
	return ResolveIncludes(path,(const char*)data,sz,0,&source_index,files_used);
}

Shader::Shader(){

};

void Shader::CreateComputeShader(const char* comp_path){
	debug->Info("Load and compile: %s ...\n",comp_path);
	std::string comp_src = LoadShaderSource(comp_path,&source_files);
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

	std::string vert_src = LoadShaderSource(vert_path,&source_files);
	if (vert_src.empty()){
		return;
	}
	std::string frag_src = LoadShaderSource(frag_path,&source_files);
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

	//A relink is a different program with a different set of surviving uniforms, so the warn-once
	//record starts over - otherwise a hot reload that introduces a missing uniform stays silent
	//about it, which is exactly the reload you want to hear about.
	reported_missing.clear();

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
/*
	One lookup, so there is one policy about a missing uniform instead of one per setter.

	That policy is: warn once, never die. The setters used to disagree with each other - Setmat3
	and Setmat4 went through debug->Fatal and so exit(1), Setint logged an error, Setfloat and
	Setvec3 logged a warning - for the same mistake. Three reasons the survivor is the mild one:

	  - A missing uniform is not evidence of a bug. GLSL strips a uniform that is declared but
	    never used, so "missing" is the ordinary outcome for a shader that legitimately does not
	    need a value the engine offers every shader generically.
	  - The fatal one fired on shaders this engine does not own. Renderer::CustomShaderPass sets
	    mat_worldcam on every registered custom shader every frame, so "all fatal" would mean any
	    custom shader with its own vertex stage that happens not to use the camera matrix killed
	    the process on the first frame - no window, and a message only on stderr.
	  - It is a render-loop call whose success depends on what the shader compiler decided to
	    eliminate. That is a poor place for exit(1).

	A caller that truly requires a uniform checks the returned bool and decides for itself, where
	the name still means something.

	Everything funnels through here, which is also the one place to add a location cache if the
	per-set glGetUniformLocation ever shows up in a profile. It has not yet.
*/
int Shader::UniformLocation(const char* name){
	//GLint, not GLuint: the not-found value is -1, which an unsigned type cannot hold. The old
	//code compared a GLuint against -1 and only worked because both sides converted to 0xFFFFFFFF.
	GLint id = glGetUniformLocation(progid, name);
	if (id == -1){
		if (reported_missing.insert(name).second){
			debug->Warn("Program %i has no uniform '%s' (declared-but-unused uniforms are stripped by the GLSL compiler)\n",progid,name);
		}
	}
	return id;
}

//Set a uniform int
bool Shader::Setint(const char* name, int value){
	int id = UniformLocation(name);
	if (id == -1){
		return false;
	}
	glProgramUniform1i(progid,id,(GLint)value);
	return true;
}

//GLSL has no bool on the wire - a uniform bool is set as an int.
bool Shader::Setbool(const char* name, bool value){
	return Setint(name,value ? 1 : 0);
}

bool Shader::Setfloat(const char* name, float value){
	int id = UniformLocation(name);
	if (id == -1){
		return false;
	}
	glProgramUniform1f(progid,id,(GLfloat)value);
	return true;
}

/*
	vec2/vec3/vec4 are plain contiguous floats - the unions inside them alias x with r, they do not
	change the layout - so both the scalar and the array forms hand the address straight to GL with
	no repacking.
*/
bool Shader::Setvec2(const char* name, const vec2& value){
	int id = UniformLocation(name);
	if (id == -1){
		return false;
	}
	glProgramUniform2fv(progid,id,1,(const GLfloat*)&value);
	return true;
}

bool Shader::Setvec3(const char* name, const vec3& value){
	int id = UniformLocation(name);
	if (id == -1){
		return false;
	}
	glProgramUniform3fv(progid,id,1,(const GLfloat*)&value);
	return true;
}

bool Shader::Setvec4(const char* name, const vec4& value){
	int id = UniformLocation(name);
	if (id == -1){
		return false;
	}
	glProgramUniform4fv(progid,id,1,(const GLfloat*)&value);
	return true;
}

bool Shader::Setmat3(const char* name, const fmat3& matrix){
	int id = UniformLocation(name);
	if (id == -1){
		return false;
	}
	glProgramUniformMatrix3fv(progid,id,1,GL_FALSE,(const GLfloat*)&matrix);
	return true;
}

bool Shader::Setmat4(const char* name, const fmat4& matrix){
	int id = UniformLocation(name);
	if (id == -1){
		return false;
	}
	glProgramUniformMatrix4fv(progid,id,1,GL_FALSE,(const GLfloat*)&matrix);
	return true;
}

/*
	The array forms. count is elements, not floats.

	Name the uniform without a subscript - "ripples", not "ripples[0]" - and GL writes the whole
	run from the array's base location. Setting more elements than the shader declares is undefined
	rather than diagnosed, so the count must come from the same constant the GLSL does; keeping
	that constant in one header shared by both is the only thing that makes these safe to use.
*/
bool Shader::Setintv(const char* name, const int* values, int count){
	int id = UniformLocation(name);
	if (id == -1 || count <= 0 || !values){
		return false;
	}
	glProgramUniform1iv(progid,id,count,(const GLint*)values);
	return true;
}

bool Shader::Setfloatv(const char* name, const float* values, int count){
	int id = UniformLocation(name);
	if (id == -1 || count <= 0 || !values){
		return false;
	}
	glProgramUniform1fv(progid,id,count,(const GLfloat*)values);
	return true;
}

bool Shader::Setvec2v(const char* name, const vec2* values, int count){
	int id = UniformLocation(name);
	if (id == -1 || count <= 0 || !values){
		return false;
	}
	glProgramUniform2fv(progid,id,count,(const GLfloat*)values);
	return true;
}

bool Shader::Setvec3v(const char* name, const vec3* values, int count){
	int id = UniformLocation(name);
	if (id == -1 || count <= 0 || !values){
		return false;
	}
	glProgramUniform3fv(progid,id,count,(const GLfloat*)values);
	return true;
}

bool Shader::Setvec4v(const char* name, const vec4* values, int count){
	int id = UniformLocation(name);
	if (id == -1 || count <= 0 || !values){
		return false;
	}
	glProgramUniform4fv(progid,id,count,(const GLfloat*)values);
	return true;
}