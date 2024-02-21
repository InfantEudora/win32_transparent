Initial running example from https://www.dhpoware.com/demos/glLayeredWindows.html

Example uses PBuffer and the old GL pipeline.

We'd like:
- [x] Multisampling to a seperate FBO
- [x] Resolve the multisample to a FBO.
- [x] Copy pixel data out of FBO instead of BPO.
- [x] Thread per window, seperate context per window.
- [ ] Use a modern pipeline and get rid of glu stuff.

wglGetProcAddress:
https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-wglgetprocaddress
The OpenGL library supports multiple implementations of its functions. Extension functions supported in one rendering context are not necessarily available in a separate rendering context.

The extension function addresses are unique for each pixel format. All rendering contexts of a given pixel format share the same extension function addresses.

So we are fine loading the extensions once, if each window uses the same pixel format.

wglMakeCurrent:
A thread can have one current rendering context. A process can have multiple rendering contexts by means of multithreading.

A thread must set a current rendering context before calling any OpenGL functions. Otherwise, all OpenGL calls are ignored.

A rendering context can be current to only one thread at a time. You cannot make a rendering context current to multiple threads.

An application can perform multithread drawing by making different rendering contexts current to different threads, supplying each thread with its own rendering context and device context.