---
name: core-features-over-workarounds
description: "When an app needs a general rendering/engine feature (background colour, ambient, etc.), add it to core rather than faking it in the app"
metadata:
  node_type: memory
  type: feedback
  originSessionId: 9ee540f3-21fa-4533-a07c-ca156fde207e
  modified: 2026-10-04T19:02:31.493Z
---

When an app hits a missing general engine feature, add it to core instead of building an app-side
workaround. 2026-10-04, chasm step 4: I planned a giant haze-coloured plane under the map because
the renderer's background colour was not settable, and a fake non-shadowing "sky" light because the
ambient term was a hardcoded 0.1*albedo - the user stopped it: "these types of features should
really exist in core instead of small workarounds".

**Why:** workarounds pile up per app and hide what the engine is missing; core features serve every app.
**How to apply:** propose/add the core feature (keeping defaults that leave existing apps unchanged).
Building and checking the other apps is NOT required for such changes - see
[[core-changes-other-apps-separately]]. Tone mapping / bloom / post-processing: the user is open to
adding them to core if a look genuinely needs them.
