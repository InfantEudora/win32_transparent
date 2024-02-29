This repository was built on:
Initial running example from https://www.dhpoware.com/demos/glLayeredWindows.html<br>
Example is from 2010 and uses uses PBuffer and the old GL pipeline.
The copyright is included in `copyright.md`

All code in this repository is supplied with the same copyright.

### Changes
- This builds on MinGW instead of VS2010 with the supplied makefile, and builds a relatively small executable.
- Simple shader instead of fixed pipeline.
- The cube is anti-aliased with smooth texture filtering.
- The white squares should be slightly see-through.
- A new thread can be spawned which creates a new window with it's own OpenGL context. Change `num_threads = 1`
- It can render to a normal window by requesting `CreateNewWindow` instead of `CreateNewLayeredWindow`
- ImagePreMultAlpha function makes no sense, removed. You can just let OpenGL do this for you with glBlendFunc

It should kind of look like this:

![screenshot](example_desktop.png)

### ToDo's
- [x] Multisampling to a seperate FBO
- [x] Resolve the multisample to a FBO.
- [x] Copy pixel data out of FBO instead of BPO.
- [x] Thread per window, seperate context per window.
- [x] Use a slightly more modern pipeline and get rid of glu stuff.
- [x] Seperate window and drawing primitive somehow
- [x] Resize to an arbitrary size
- [ ] Share the texture across contexts?
- [x] Use named versions of Frame/Renderbuffers
- [x] Continue rotating while being dragged...
- [x] Add another instance of cube
- [x] Add another object type... sphere?
- [x] Move something with input
- [x] Compute shader to generate the texture
- [x] Have some kind of global list of materials somewhere
- [x] Assign different textures to materials
- [ ] Assign different materials to objects by storing their material index in a sperate VAO.
- [x] A seperate physics thread that updates object positions and manages input
- [x] A scene to put things in.
- [ ] Scale window and framebuffers
- [x] Simple lighting, but keep in mind it needs to be PBR at some point.
- [ ] Attempt to put ImGui in and keep the crazy overlay going.
- [ ] Object selection based on ID with a buffer...? SSBO read/write from FS doesn't work optimally.
- [ ] Since we want deferred rendering for SSAO, it will solve the ID selection problem.
- [x] Make the hovered / selected object use a certain material.
- [x] Blue and Red are reversed somewhere.. somehow.
- [x] All files / assets that are loaded from file should be compilable into the application by some kind of asset manager.

### Some notes

https://github.com/fendevel/Guide-to-Modern-OpenGL-Functions

```wglGetProcAddress```<br>
https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-wglgetprocaddress
The OpenGL library supports multiple implementations of its functions. Extension functions supported in one rendering context are not necessarily available in a separate rendering context.

The extension function addresses are unique for each pixel format. All rendering contexts of a given pixel format share the same extension function addresses.

So we are fine loading the extensions once, if each window uses the same pixel format.

```wglMakeCurrent```<br>
A thread can have one current rendering context. A process can have multiple rendering contexts by means of multithreading.

A thread must set a current rendering context before calling any OpenGL functions. Otherwise, all OpenGL calls are ignored.

A rendering context can be current to only one thread at a time. You cannot make a rendering context current to multiple threads.

An application can perform multithread drawing by making different rendering contexts current to different threads, supplying each thread with its own rendering context and device context.

https://www.shadertoy.com/view/ld2Gz3


Deferred shading from LearnOpenGL, or anything, doesn't use MSAA. Because... what's the position or normal for a fragment that's a blend of different fragments...? Actually... why wouldn't it? It can blend normals, the blended edges just get a curved normal. Blending to transparent would probably be weird...


### Input
Input can be fetched from the messages sent to a window, but this ties the input thread to a different thread than the render thread.

### Tested
Video Cards:
 - Intel HD Graphics 520 (Thinkpad T460s)
 - NVidia RTX 2070
 - AMD FirePro M4000 (Elitebook 8570W)

Fragment shader writes ObjectID to a SSBO when the fragment at mouse position is being rendered. This has some problems with Z testing.