# testfx channel textures

Drop `.png`, `.jpg`, `.tga` or `.bmp` files here and they appear in the Effect panel's four
`iChannel` combo boxes. Nothing is loaded until a channel is pointed at it.

`iChannel0` defaults to the seeded noise from `Application::rrand` rather than to a file — 256×256
single-channel bytes, uploaded once at startup. Preferring that over a GLSL hash is deliberate and
not only about speed: the bytes are reproducible, the simulation can draw from the *same* stream,
and a value read in a shader can be checked against the same value read in C++. A hash function in
GLSL can be none of those. See `core/RRandom.h`.

This file exists so the folder does — `Directory::GetFiles` resolves a folder through the asset
search path and an empty directory that git has dropped would be reported as missing.
