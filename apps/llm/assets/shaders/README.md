# llm shaders

The compute shaders of the model's forward pass, one `.comp` per step (matrix times vector,
rmsnorm, RoPE, attention, SwiGLU). They are this app's own until a game uses them; then they
move to `shared_assets/shaders/`, which holds only what core loads or two or more apps use.

Like every shader in this engine they carry no `#version` line - the engine prepends
`#version 430 core` (see `core/Shader.cpp`).
