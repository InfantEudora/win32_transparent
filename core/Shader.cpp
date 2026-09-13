#include "glad.h"
#include <vector>
#include <mutex>
#include "Shader.h"
#include "Debug.h"
#include "File.h"
#include <string>

static Debugger *debug = new Debugger("Shader", DEBUG_INFO);

/*
	The registry behind Shader::ForEachShader.

	Function-local statics rather than file-scope objects: several translation units construct
	Shader instances from their own file-scope initialisers, and a plain static vector might not be
	constructed yet when the first of those runs. This form is initialised on first use, whenever
	that turns out to be.
*/
static std::mutex& RegistryMutex(){
	static std::mutex m;
	return m;
}
static std::vector<Shader*>& Registry(){
	static std::vector<Shader*> list;
	return list;
}

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
	std::lock_guard<std::mutex> lock(RegistryMutex());
	Registry().push_back(this);
};

void Shader::CreateComputeShader(const char* comp_path){
	BuildCompute(comp_path);
};

bool Shader::BuildCompute(const char* comp_path){
	debug->Info("Load and compile: %s ...\n",comp_path);

	//A compute program has no vertex stage, so its path lives in fname - see f_is_compute.
	fname = comp_path;
	vname.clear();
	f_is_compute = true;
	compile_log.clear();
	f_compiled = false;

	//A local list assigned at the end, because a rebuild must REPLACE the source set rather than
	//append to whatever the previous build left behind.
	std::vector<std::string> files;
	std::string comp_src = LoadShaderSource(comp_path,&files);
	if (!files.empty()){
		source_files = files;
	}
	if (comp_src.empty()){
		compile_log = std::string("Could not read ") + comp_path + "\n";
		progid = -1;
		return false;
	}

	debug->Info("Compiling compute shader : %s\n", comp_path);
	int compid = CompileCompute((char*)comp_src.c_str(),comp_src.size());
	if (compid == 0){
		progid = -1;
		return false;
	}
	debug->Info("Linking program\n");
	int linked = LinkProgram(1,compid);
	if (linked == -1){
		progid = -1;
		return false;
	}
	progid = linked;
	f_compiled = true;
	return true;
}

Shader::Shader(const char* vert_path,const char* frag_path):Shader(){
	Build(vert_path,frag_path);
}

bool Shader::Build(const char* vert_path,const char* frag_path){
	debug->Info("Load and compile: %s, %s ...\n",vert_path,frag_path);

	vname = vert_path;
	fname = frag_path;
	f_is_compute = false;
	compile_log.clear();
	f_compiled = false;

	//Both files are read before either is compiled, and the list is assigned whatever happens: a
	//later Reload has to be able to release what this attempt actually loaded, including on the
	//run where the second file was the one that went missing.
	std::vector<std::string> files;
	std::string vert_src = LoadShaderSource(vert_path,&files);
	std::string frag_src = LoadShaderSource(frag_path,&files);
	if (!files.empty()){
		source_files = files;
	}
	if (vert_src.empty() || frag_src.empty()){
		//LoadFile has already named the file it could not read, on stderr. This is the same news
		//for a caller that cannot see stderr.
		compile_log = std::string("Could not read ") +
					  (vert_src.empty() ? vert_path : frag_path) + "\n";
		progid = -1;
		return false;
	}

	debug->Info("Compiling vertex shader : %s\n", vert_path);
	int vertid = CompileVertex((char*)vert_src.c_str(),vert_src.size());
	debug->Info("Compiling fragment shader : %s\n", frag_path);
	int fragid = CompileFragment((char*)frag_src.c_str(),frag_src.size());

	//A failed compile comes back as 0, and glAttachShader(program,0) is a GL error - so a build
	//with one broken stage is abandoned rather than linked with a hole in it. Unreachable while
	//f_fatal_on_error is set, since the compile has already exited by then; that is exactly why
	//it was safe to leave out until the soft path existed.
	if ((vertid == 0) || (fragid == 0)){
		if (vertid != 0){
			glDeleteShader(vertid);
		}
		if (fragid != 0){
			glDeleteShader(fragid);
		}
		progid = -1;
		return false;
	}

	debug->Info("Linking program\n");
	int linked = LinkProgram(2,vertid,fragid);
	if (linked == -1){
		progid = -1;
		return false;
	}
	progid = linked;
	f_compiled = true;
	return true;
}

bool Shader::Reload(){
	if (fname.empty()){
		compile_log = "This program was not built from files, so there is nothing to reload\n";
		return false;
	}

	/*
		Give the files back before reading them, or this reloads nothing. The whole source set,
		not the one or two filenames - see the note on Reload in Shader.h, and ReleaseFile in
		core/File.h for what the three answers mean.
	*/
	int num_embedded = 0;
	for (const std::string& path:source_files){
		if (ReleaseFile(path.c_str()) == FILE_RELEASE_EMBEDDED){
			num_embedded++;
		}
	}
	if (num_embedded > 0){
		char msg[256];
		snprintf(msg,sizeof(msg),
				 "Not reloaded: %i of this shader's %zu source files are baked into this build\n",
				 num_embedded,source_files.size());
		compile_log = msg;
		debug->Info("%s",msg);
		return false;
	}

	//Always soft. See the block above Reload in Shader.h for why the flag does not get a say.
	bool f_was_fatal = f_fatal_on_error;
	f_fatal_on_error = false;

	//Keep the working program until the new one links, so a typo leaves the last good picture on
	//screen instead of a black one.
	int previous_prog = progid;
	bool f_was_compiled = f_compiled;

	bool f_ok = f_is_compute ? BuildCompute(fname.c_str()) : Build(vname.c_str(),fname.c_str());

	f_fatal_on_error = f_was_fatal;

	if (!f_ok){
		progid = previous_prog;
		f_compiled = f_was_compiled;
		debug->Err("Reload of %s failed, keeping program %i\n",fname.c_str(),progid);
		return false;
	}
	if ((previous_prog != -1) && (previous_prog != progid)){
		glDeleteProgram(previous_prog);
	}
	debug->Ok("Reloaded %s (program %i)\n",fname.c_str(),progid);
	return true;
}

void Shader::ForEachShader(const std::function<void(Shader*)>& fn){
	if (!fn){
		return;
	}
	std::lock_guard<std::mutex> lock(RegistryMutex());
	for (Shader* s:Registry()){
		fn(s);
	}
}

Shader::~Shader(){
	std::lock_guard<std::mutex> lock(RegistryMutex());
	for (size_t i = 0;i < Registry().size();i++){
		if (Registry().at(i) == this){
			Registry().erase(Registry().begin() + i);
			break;
		}
	}
};

//One place that decides what a build stage's info log does, so the four of them cannot disagree
//about it the way they used to about a missing uniform.
void Shader::RecordLog(const char* stage, const char* log){
	if (!log || (*log == '\0')){
		return;
	}
	compile_log += stage;
	compile_log += ": ";
	compile_log += log;
	if (compile_log.back() != '\n'){
		compile_log += '\n';
	}
}

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
		//Terminated before the driver is asked to fill it: an implementation that reports an info
		//log length of zero would otherwise leave this buffer uninitialised and every reader of it
		//looking at whatever was on the heap.
		char* errormsg = (char*)malloc(infolen+1);
		errormsg[0] = '\0';
		glGetShaderInfoLog(id, infolen, NULL, errormsg);
		RecordLog("CompileVertex",errormsg);
		//Fatal exits, so the soft path is everything after it - see Shader::f_fatal_on_error.
		if (f_fatal_on_error){
			debug->Fatal("CompileVertex: error: %s\n", errormsg);
		}
		debug->Err("CompileVertex: error: %s\n", errormsg);
		free(errormsg);
		glDeleteShader(id);
		return 0;
	}
	if (infolen > 1){
		char* errormsg = (char*)malloc(infolen+1);
		errormsg[0] = '\0';
		glGetShaderInfoLog(id, infolen, NULL, errormsg);
		RecordLog("CompileVertex",errormsg);
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
		errormsg[0] = '\0';
		glGetShaderInfoLog(id, infolen, NULL, errormsg);
		RecordLog("CompileFragment",errormsg);
		if (f_fatal_on_error){
			debug->Fatal("CompileFragment: error: %s\n", errormsg);
		}
		debug->Err("CompileFragment: error: %s\n", errormsg);
		free(errormsg);
		glDeleteShader(id);
		return 0;
	}
	if (infolen > 1){
		char* errormsg = (char*)malloc(infolen+1);
		errormsg[0] = '\0';
		glGetShaderInfoLog(id, infolen, NULL, errormsg);
		RecordLog("CompileFragment",errormsg);
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
		errormsg[0] = '\0';
		glGetShaderInfoLog(id, infolen, NULL, errormsg);
		RecordLog("CompileCompute",errormsg);
		if (f_fatal_on_error){
			debug->Fatal("CompileCompute: error: %s\n", errormsg);
		}
		debug->Err("CompileCompute: error: %s\n", errormsg);
		free(errormsg);
		glDeleteShader(id);
		return 0;
	}
	if (infolen > 1){
		char* errormsg = (char*)malloc(infolen+1);
		errormsg[0] = '\0';
		glGetShaderInfoLog(id, infolen, NULL, errormsg);
		RecordLog("CompileCompute",errormsg);
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

	va_list arglist;
    va_start(arglist,count);
	std::vector<int>ids;
    for (int i = 0; i < count; ++i) {
        int id = va_arg(arglist, int);
		ids.push_back(id);
    }
    va_end(arglist);

	//Collected before anything is created, because a stage that failed to compile comes back as 0
	//and glAttachShader(program,0) is a GL error. Every caller here already checks, so this is the
	//backstop for the next one; it is cheap and it fails loudly instead of into the GL error queue.
	for (int id:ids){
		if (id == 0){
			RecordLog("LinkProgram","a shader stage did not compile, so nothing was linked");
			debug->Err("LinkProgram: a stage compiled to 0 - not linking\n");
			return -1;
		}
	}

	GLuint programid = glCreateProgram();
	for (int id:ids){
		glAttachShader(programid, id);
		debug->Info("LinkProgram: Attaching ID %i\n",id);
	}
	glLinkProgram(programid);

	// Check the program
	glGetProgramiv(programid, GL_LINK_STATUS, &result);
	glGetProgramiv(programid, GL_INFO_LOG_LENGTH, &infolen);
	if (!result){
		char* errormsg = (char*)malloc(infolen+1);
		errormsg[0] = '\0';
		glGetProgramInfoLog(programid, infolen, NULL, errormsg);
		RecordLog("LinkProgram",errormsg);
		if (f_fatal_on_error){
			debug->Fatal("LinkProgram: error: %s\n", errormsg);
		}
		debug->Err("LinkProgram: error: %s\n", errormsg);
		free(errormsg);
		//-1, NOT 0. Every progid check in this repo reads `progid != -1`, so returning 0 here -
		//which is what this did, behind a Fatal that made it unreachable - would sail straight
		//through those guards and leave callers drawing with program 0.
		for (int id:ids){
			glDetachShader(programid, id);
			glDeleteShader(id);
		}
		glDeleteProgram(programid);
		return -1;
	}
	if (infolen > 1){
		char* errormsg = (char*)malloc(infolen+1);
		errormsg[0] = '\0';
		glGetProgramInfoLog(programid, infolen, NULL, errormsg);
		RecordLog("LinkProgram",errormsg);
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