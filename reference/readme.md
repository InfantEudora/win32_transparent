# Reference

Material kept to **read**, never to compile: a shadertoy excerpt, a snippet from another engine,
a technique written up somewhere worth keeping. Inspiration and worked examples, not build input.

Not called `examples/`, because the apps under `/apps` are the examples this engine ships with -
that name is taken. See `docs/asset_layout_plan.md` §7.

## Two rules, so this does not rot into another `temp/`

1. **Nothing here ever appears in `APP_SRCS`, `LIB_DIRS`, `IPATHS` or a `LoadFile` call.** The
   build cannot reach it, so it cannot silently break - and that is the entire difference between
   this folder and the `temp/` it is meant to replace.

2. **Every file gets a provenance header**: where it came from, its licence, and why it is kept.
   A snippet with no attribution is a liability the moment anyone pastes it into something that
   ships, and by then nobody remembers where it came from.

## Contents

### `shaders/`

`shadertoy_nixie_tube.hlsl`, `shadertoy_smoke_lights.hlsl` - HLSL, so not compilable by this
engine's GLSL pipeline; kept for the technique. They previously sat in `/shaders` among the real
ones, where the second had its extension misspelled `.hsls` - decent evidence that nothing had
ever loaded it. **Neither carries any attribution yet.** Whoever knows where they came from
should add a header before either gets used.

### `code_snippets/`

What used to be loose in `temp/`: `PieMenu`, `glad_wgl`, `interp.h`, `perlin`, `noisegen`, and the
`ImCurveEdit`/`ImSequencer` widgets that sat in the repo root for years wired into nothing. All of
it was tracked in git and built by no app.

Most of it has no provenance header yet - see rule 2.
