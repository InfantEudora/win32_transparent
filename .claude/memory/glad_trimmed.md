---
name: glad-trimmed
description: "core/glad.h is a trimmed loader - some core GL constants and entry points (GL_RGBA32UI, GL_RGBA32I, glClearNamedFramebufferuiv) are missing"
metadata:
  node_type: memory
  type: project
  originSessionId: f74358c8-36a9-48cf-a846-52036d5d2ea0
  modified: 2026-10-06T15:03:38.028Z
---

core/glad.h does not carry every GL 4.6 name. Found 2026-10-06 building the outline pass: GL_RGBA32UI and
GL_RGBA32I are undefined and glClearNamedFramebufferuiv does not exist (glClearNamedFramebufferiv does).

**Why:** the loader was generated with a reduced set; a format constant is just a number the driver knows,
but a missing entry point cannot be called at all.

**How to apply:** for a missing format constant, `#ifndef GL_X / #define GL_X 0x....` locally (see
Renderer::RebuildOutlineFBO); for a missing function, use one glad has (signed instead of unsigned
integer targets) rather than regenerating glad.
