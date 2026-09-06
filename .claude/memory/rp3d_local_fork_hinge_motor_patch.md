---
name: rp3d-local-fork-hinge-motor-patch
description: "reactphysics3d fork (InfantEudora, branch crane_testbed, CMake+Ninja from C:/code/reactphysics3d) carries 3 local patches + 1 merged PR; libs/libreactphysics3d-0.10.2.a is not stock upstream; Jolt checkout at C:/code/JoltPhysics"
metadata: 
  node_type: memory
  type: project
  originSessionId: ac29a5ca-063a-4012-9f01-e719344e12da
  modified: 2026-09-04T18:30:00.000Z
---

`libs/libreactphysics3d-0.10.2.a` is built from the user's rp3d fork (github InfantEudora/reactphysics3d, branch `crane_testbed`, upstream v0.10.2). Two identical checkouts exist and were in sync on 2026-09-04: `C:/code/reactphysics3d` (the one CMake was configured from - build.ninja hardcodes C:/code paths) and `C:/IDE-E/Mijn Documenten/Projects/code/test/reactphysics3d`. Prefer `C:/code/reactphysics3d` for builds. Build is CMake + Ninja now (NOT the old MSYS Makefiles): `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DRP3D_COMPILE_TESTBED=ON -DCMAKE_EXE_LINKER_FLAGS="-static -static-libgcc -static-libstdc++"` then `cmake --build build -j` (run with `C:/msys64/mingw64/bin` on PATH). Produces `build/libreactphysics3d.a` (copy over `libs/libreactphysics3d-0.10.2.a`, then `rm wind.exe` and relink - the makefile doesn't track the lib) and `build/testbed/testbed.exe` (nanogui/GLFW testbed; user added a Crane scene under testbed/scenes/crane). Unit tests: `test/` with a hand-rolled TestSuite, wired in `test/CMakeLists.txt` + `test/main.cpp`. The stock-lib backup `.orig-hinge-motor-bug` has been deleted. A Jolt checkout (v5.6.0+) sits at `C:/code/JoltPhysics` - its `Jolt/Physics/Vehicle/` is the reference design for a planned rp3d wheel/suspension constraint (see [[rp3d-vehicle-constraint-plan]]).

**Local patches (all 2026-09-02, all marked "(local fix)" in comments, all still present in upstream master):**
1. `src/systems/SolveHingeJointSystem.cpp` motor: impulse applied with sign opposite to its Jacobian (`JvMotor = a1.(w1-w2)`) -> positive feedback, relative angular velocity doubles per iteration until pinned at maxMotorTorque. Flipped the two solve-step signs + warm-start `motorImpulse`. Slider motor was the correct reference.
2. `src/constraint/HingeJoint.cpp` + `SolveHingeJointSystem::computeCurrentHingeAngle`: relative rotation was world-frame `q2*q1^-1` (conjugated by any common rotation) -> hinge angle and LIMITS read wrong whenever body1 turns from its creation orientation (hinge on a vehicle, moved/yawed base). Now body1-frame `q1^-1*q2`, sign test against body1-LOCAL axis.
3. `src/body/RigidBody.cpp` setType(STATIC) and setIsSleeping(true): also zero `mConstrained{Linear,Angular}Velocities` - the solver reads those for both joint bodies and they're only refreshed for enabled bodies, so a body made static while moving kept "moving" for every joint on it.

Plus a merged upstream PR (GrzegorzSzczodrzec, commit 3327ff94): angular velocity integration in the body's local frame via Euler's equations, with a unit test in `test/tests/systems/TestDynamicSystem.h`.

**Why:** Hinge joints on anything but an identity-oriented, never-moving static body were unusable on stock rp3d 0.10.2. User has hit other rp3d bugs before, considers it semi-abandoned but functional; expect more.

**How to apply:** Never replace the lib with a stock build without re-applying the patches. If joints misbehave, first check `libs/` is the patched build. Patches are committed on branch `crane_testbed` (commit e8df5337). Diagnose joint problems with the app's MCP telemetry over HTTP (`curl localhost:8765/mcp`), e.g. `crane_telemetry`, `object_get` - stale nonzero velocity with frozen position was the tell for #3.

Related: [[build-toolchain-location]].
