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

### ToDo's
- [x] Multisampling to a seperate FBO
- [x] Resolve the multisample to a FBO.
- [x] Copy pixel data out of FBO instead of BPO.
- [x] Thread per window, seperate context per window.
- [x] Use a slightly more modern pipeline and get rid of glu stuff.
- [ ] Seperate window and drawing primitive somehow
- [x] Resize to an arbitrary size
- [ ] Share the texture across contexts?

### Some notes

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