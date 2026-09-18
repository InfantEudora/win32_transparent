# Brief: build a paddle game (Breakout) on this engine

You are building a game on `win32_transparent`, a hand-rolled C++17/OpenGL engine that its author
has grown over several years as a collection of experiments sharing one core. The core has
matured. The question this task answers, again and from a fresh angle, is **whether a real game
can be built on it, what that takes, and what is missing when you try.**

Read this document top to bottom before writing code. Everything in it was checked against the
working tree at commit `d22ca00` (which carries uncommitted work on the renderer, the occluder
field shaders and the Tetris app), and where it cites a file and line, that line was read. Line
numbers drift; the surrounding text is the real reference.

### One thing you must not read

**Do not open `docs/tetris_agent_brief.md` or `docs/tetris_findings.md`.** They are a previous
agent's brief and report. Parts of them are now wrong — features they call missing exist, bugs
they list are fixed — and more importantly, the value of this run is a second *independent* look
at the engine. A finding you reach yourself is worth something; a finding you inherit is not.

Everything else in the repository is yours to read, and you should read a lot of it. In
particular `ApplicationTetris.cpp` + `tetris/` is the most recent worked example of a 2D game on
this 3D engine and answers most "how do I..." questions by demonstration. `ApplicationShip.cpp`
is the best example of custom shaders, contact callbacks and gameplay physics.
`docs/engine_backlog.md` is the live list of what is known-broken and what was recently fixed —
read it, and check it before reporting something as a new find.

---

## 1. The task, and the three deliverables

### Deliverable 1 — a playable paddle game

A new app, `APP=Breakout`, that builds and runs alongside the eleven existing ones, and that a
person can sit down and play with a keyboard.

"A paddle game" means the genre in the classic sense — the family containing *Breakout*,
*Arkanoid*, *Pong*'s single-player descendants. The screenshot the author had in mind is the
familiar one: a rack of coloured bricks, a ball, a paddle sliding along the bottom.

**The floor, non-negotiable:** a paddle the player controls, a ball that moves and bounces, a
field of destructible bricks, a losing condition, a way to progress or win, and a score.
Someone who has never seen your code must recognise it as a paddle game within two seconds of
looking at the screen.

**Above that floor, be inventive.** It does not have to look like the screenshot, it does not
have to be flat-on, it does not have to obey the classic's rules, and the bricks do not have to
be bricks. You have a 3D renderer with real lights, real shadows, a rigid-body solver, sound and
a custom shader stage — a game that uses none of that and could have been written in 200 lines of
SDL is a poor answer to the question being asked. Pick a small number of ideas that the *engine*
makes good and commit to them, rather than sprinkling features thinly.

### Deliverable 2 — an effect you wrote yourself, as a custom shader

**A hard requirement, not a stretch goal.** At least one visual effect in the game must be a
fragment shader you wrote, assigned to geometry through the renderer's custom-material pass. Not a
tweak to `default.frag`, not a material colour, not a particle emitter: your own `.frag` file in
`shaders/`, registered with `Renderer::AddCustomShader`, drawn on a mesh you tagged
`MESH_MODE_SHADER`.

It must be **driven by the game**: at least one uniform whose value comes from live simulation
state and changes as the game is played. A static pattern is a texture with extra steps.

Section 6 is the whole mechanism, with a worked example and the traps.

### Deliverable 3 — an engineering report

`docs/breakout_findings.md`. This is the part the author actually asked for, and it carries equal
weight with the game. Everywhere the engine fought you, write it down: a missing feature, a bug,
an API that could not express what you needed, boilerplate that should have been one call,
a comment or a document that was wrong. Be specific and reproducible — file, line, what you
expected, what happened, what you did instead. **A finding you worked around still counts and must
still be reported.** Section 10 gives the structure.

Write it as you go. The findings you lose are the ones you were sure you would remember.

**Do not sacrifice the report to finish the game.** A slightly smaller game with an honest report
is the better outcome.

### Ground rules

- **You may patch `core/`, but every change must be logged.** Keep a "Changes to `core/`" section
  in the findings with one entry per change: file, what you changed, why, and what it would have
  cost you not to. *Prefer a workaround in your own app when the two are comparable* — an
  unpatched gap is a clearer signal than a patched one. Any change that alters behaviour for the
  other apps must be called out loudly, and you must confirm the other apps still build
  (`for a in Animation Dozer Grid IsoAnimation OCPP Ship Sim Tank Tetris Tileset UI; do
  mingw32-make.exe APP=$a -j8 || echo "FAILED $a"; done` — note this relinks `wind.exe` each time,
  so nothing may be running).
- **Do not modify the other apps** (`Application{Ship,Tank,Grid,Dozer,Tileset,Sim,Animation,IsoAnimation,OCPP,UI,Tetris}.cpp`
  or their folders). Read them freely — they are the best documentation in the repo.
- **The skeletal animation system is unfinished and is not part of this task.** `ObjectAnimation.cpp`
  still has a `debug->Fatal` for any clip carrying a scale track, and the root-motion rewrite is
  mid-flight. Do not build gameplay on skinned characters or imported animation clips. Everything
  else — physics, sound, lighting, shadows, text, input, the command queue, MCP — is in usable
  shape and you are expected to use it. Motion that you animate *yourself* (per-tick maths, or
  `Scene::MoveObjectOverTicks`) is not the animation system and is entirely fine.
- **The code style is not yours to change.** 4-space indent, `f_` prefix on booleans, `//`
  comments that explain *why* rather than *what*. This codebase comments unusually heavily and
  unusually well; write in that register. A change that explains its own reasoning fits; one that
  narrates the mechanism does not.
- **Durations are counted in ticks, never in milliseconds.** See §7.5. This is the single
  house rule most likely to bite you if you ignore it.

---

## 2. The mental model in sixty seconds

One executable, `wind.exe`. `APP=X` at build time selects which `Application` subclass `main.cpp`
instantiates. There is no scripting layer, no editor and no scene file format — **a scene is built
in C++ in your app's `Init()`.**

Three threads, created in this order:

| Thread | Created by | Runs | Owns |
|---|---|---|---|
| **Main** | `WinMain` | `Application::Start()`, a Win32 message pump | Window messages only. Blocks while the title bar is dragged. |
| **Render** | `Application::Start()` | `Init()` once, then `PreRender` → `DrawFrame` → `DrawImGuiUI` forever | **The OpenGL context. Every GL call must happen here.** |
| **Physics** | the render thread, after `Init()` returns | the simulation loop, paced to `physics_tps` | The simulation. |

Plus a Raw Input thread and one or two MCP server threads, neither of which touches simulation
state except through the queues in §7.10.

One pass of the physics loop, in order (`core/Application.cpp`, `PhysicsThreadFunction`):

```
UpdateInput()                      // samples devices, applies the event stream
Scene::BeginPass()                 // drains commands, services the pause key,
                                   //   decides ONCE whether this pass ticks
  if (ticking) {
      UpdateAnimations()
      RunSimulationTick()          // <-- YOUR GAMEPLAY GOES HERE
      UpdatePhysics()              // <-- rp3d steps the world
  }
UpdateView()                       // every pass, ticking or not
```

The two hooks you override, and the test for which one a piece of code belongs in:

- **`RunSimulationTick()`** — exactly once per tick that actually runs, immediately before the
  physics step. Gameplay. It does not run while paused, and single-stepping runs it exactly once
  per step. *If running it twice for a single tick would change the outcome, it is simulation and
  it goes here.*
- **`UpdateView()`** — every pass of the loop, including passes that simulate nothing because the
  game is paused. Camera, picking, editor gizmos, a debug marker. It must not change anything a
  tick will read: it runs a number of times that depends on loop pacing and on how long the game
  sat paused.

Both run on the physics thread with `renderer->physics_mutex` held. Neither may touch GL.

`DrawImGuiUI()` runs on the **render thread** with that same mutex held, so it can read simulation
state directly — but it must never *wait* on the physics thread (see §7.10).

Two more hooks:

- **`Init()`** — render thread, once. All GL, all asset loading, all mesh building, all
  registration.
- **`PreRender()`** — render thread, top of every frame, before anything is drawn. For GL work
  that has to happen before the colour pass: filling a texture, rebuilding a mesh (a score label,
  say) from state the tick produced.

---

## 3. Build and run

### Toolchain

The compiler is **not on the default PATH**:

```bash
export PATH="/c/msys64/mingw64/bin:$PATH"
mingw32-make.exe APP=Breakout -j8        # mingw32-make.exe, NOT /usr/bin/make
```

Header dependencies are tracked (`-MMD -MP`), so editing a header rebuilds its dependents.

### Running

```bash
./wind.exe 2>wind_stderr.log &
```

- **`stderr` carries all logging. `stdout` is reserved for the MCP stdio transport and must stay
  clean.** Never `printf` to stdout; use the `Debugger` (see below).
- **The linker cannot overwrite a running `wind.exe`.** A build while it is running fails with
  `cannot open output file wind.exe: Permission denied`. Stop it first:
  `taskkill //F //IM wind.exe`. The same error with nothing running means a stale lock — deleting
  `wind.exe` clears it.
- **Unexplained heap corruption (`c0000374`) after switching `APP` means stale objects.** Run
  `mingw32-make.exe clean` and rebuild.

Logging, the pattern every file in the repo uses — one static `Debugger` per translation unit,
named after it:

```cpp
static Debugger* debug = new Debugger("ApplicationBreakout",DEBUG_ALL);
...
debug->Info("Ball reset, lives = %i\n",lives);
```

`debug->Fatal(...)` prints and then calls `exit(1)` (`core/Debug.cpp:149`). That is not a
figure of speech — several engine paths reach it, and §9 lists the ones that will catch you.

### Shell traps (these have each cost more than one session)

- **Always `<<'EOF'`, never `<<EOF`** for heredocs. And note a quoted delimiter still collapses
  `\\` to `\`, so **any text containing a backslash must not go through a heredoc at all** — use a
  file-writing tool. A C string `"...\n"` written through a heredoc arrives with a real newline
  inside the literal and fails to compile.
- **Keep any single shell command under ~30 KB.** Over that, `CreateProcess` fails with
  `ENAMETOOLONG: name too long, uv_spawn`, which looks like a path problem and is not.
- **The working directory can reset between tool calls.** Prefer absolute paths.
- **`&&` after a native `.exe` is unreliable** — check `$?` explicitly if the exit status matters.

---

## 4. Creating the app

Five things, and then it builds.

**1. `apps/Breakout.mk`** — the build fragment that selects your app:

```make
APP_HEADER := ApplicationBreakout.h
APP_CLASS  := ApplicationBreakout

SRCS    += ApplicationBreakout.cpp
IPATHS  += -Ibreakout/
DIR_SRC += ./breakout

#The ball, the bricks and the paddle make noise - see core/SoundSystem.h.
USE_SOUND := 1
```

`DIR_SRC` is wildcarded, so **every `.cpp` you drop in `breakout/` is compiled automatically** —
no per-file edits. `USE_SOUND := 1` is what links OpenAL; without it `core/SoundSystem.cpp` is
dropped from the build entirely.

**2. `ApplicationBreakout.h` / `.cpp`** in the repo root, subclassing `Application`. The root is
where every app's class lives; its gameplay types go in the folder.

**3. `breakout/`** — the rules, as plain C++ that knows nothing about the engine. This split is
worth taking seriously and is what `tetris/Playfield.{h,cpp}` does: the rules are an array and
some tick counters with no `Object`, no `Renderer` and no GL anywhere in them, and the
`Application` subclass is the *view* plus the wiring. It makes the rules testable, it makes the
"is this a rules bug or a rendering bug?" question answerable, and it keeps the app file from
becoming three thousand lines.

**4. Your shader(s)** in `shaders/`. They are loaded from disk at runtime by relative path, so
nothing in the build needs to know about them.

**5. Your findings doc**, started on day one, not on the last day.

The minimum `Init()` that produces a lit, visible, non-black window:

```cpp
void ApplicationBreakout::Init(void){
    renderer = new Renderer(main_window->width,main_window->height);
    //PIPELINE_DEFERRED, not PIPELINE_MSAA: the deferred pass is what fills the object-id
    //buffer, and so what makes mouse picking and the Inspector work. It is also what the
    //custom shader pass reads to know how far away the scene is.
    if (!renderer->Init(PIPELINE_DEFERRED)){
        debug->Fatal("Failed to initialise rendering pipeline\n");
    }
    renderer->SetVSync(true);

    default_shader = new Shader("shaders/default.vert","shaders/default.frag");

    //Still constructed even if you load no assets: the engine reaches for it unguarded in
    //places (the Scene panel's asset list, the object_spawn command handler).
    assetmanager = new AssetManager();

    main_scene = CreateNewScene("Breakout");
    main_scene->physics_world = new PhysicsWorld();
    main_scene->physics_world->SetGravity(vec3(0,-9.81f,0));

    //Without a light, everything renders black.
    DirectionalLight* sun = new DirectionalLight();
    sun->name = "Sun";
    sun->SetPosition(vec3(-6,24,16));
    sun->SetLookAt(vec3(0,0,0));
    sun->brightness = 4.5f;
    sun->viewport.zoom = 26;    //shadow ortho half-extent, in world units - must cover the play area
    main_scene->AddObject(sun);

    main_window->Resize(1200,900);
}
```

`CreateNewScene` gives you a scene with a perspective `Camera` already in it at
`main_scene->camera` (`core/Application.cpp:2900`).

---

## 5. The shape of a paddle game on this engine

### There is no 2D renderer, and you do not want one

Everything is a mesh in a 3D world with real lights and real shadows. The way a flat game is made
to read as flat here is the **camera**, not the geometry:

```cpp
Camera* camera = main_scene->camera;
camera->SetType(CAMERA_TYPE_ORTHOGRAPHIC);
//zoom is the VERTICAL half-extent in world units; horizontal follows from the aspect ratio.
camera->SetupOrthographic(renderer->width,renderer->height,11.5f,0.1f,120.0f);
camera->SetPosition(vec3(target.x,target.y,40.0f));
camera->SetLookAt(target);
camera->CalculateLookatMatrix();
```

That is Tetris's camera: orthographic, looking straight down `-Z` at the `XY` plane, so **board
coordinates are world coordinates** and nothing needs converting. It is a good default and it
costs you nothing to copy.

But it is a *default*, not a requirement, and this is one of the places to be creative. A slight
perspective tilt makes the bricks solid objects with sides and shadows instead of coloured
rectangles. The play field does not have to be the `XY` plane. `viewport.zoom` on a
`DirectionalLight` is the half-extent of the shadow map's orthographic frustum — if your play area
is 24 units wide, a zoom of 10 means half your bricks cast no shadow at all, which reads as a
lighting bug and is not one.

`Renderer::viewport_x/y/width/height` confine the 3D draw to a sub-rectangle of the window while
ImGui keeps the rest — useful if you want the engine's debug panels beside the game rather than
on top of it. Note `viewport_y` is measured from the **bottom** (GL's origin), deliberately not
flipped to match the window's top-left convention.

### The one real design decision: what is the ball?

This is the interesting engineering question in a paddle game on an engine with a rigid-body
solver, and **your findings must say which you chose and why.**

**Option A — the ball is an rp3d dynamic body.** Bricks, walls and the paddle get colliders; you
set the ball bouncing and let the solver do the rest; you learn what was hit from an `onContact`
callback. You get sweep-free continuous behaviour for free... except you do not (rp3d here is
discrete, and a fast small sphere tunnels through a thin brick). You get emergent spin, angled
deflections and the paddle shoving the ball, all without writing any of it. You give up exact
control of the bounce angle, which is the single most important feel parameter in the genre — the
classic's "hit the edge of the paddle, get a steeper angle" is a *designed* response, not a
physical one.

**Option B — the ball is your own integration.** Position and velocity you advance yourself each
tick, with your own swept circle-vs-rectangle test against the brick grid. Exact, deterministic,
tunnel-proof, and the bounce angle is whatever you decide it is. You then have a physics engine in
the scene doing nothing for your core loop — so if you go this way, find something real for it to
do (debris from a shattered brick, a wrecking-ball power-up, bricks that fall when the row beneath
them goes) rather than turning physics off and reporting that you did not use it.

**Option C — a hybrid**, which is what most shipped games in this genre actually do: your own
motion and collision for the ball, rp3d for everything cosmetic and for the things that benefit
from being physical.

There is no right answer and the author wants to know what happened. If you take A and the ball
tunnels, that is a finding. If you take B, the honest note that the solver was not useful for the
core loop is also a finding. **Measure rather than assume**: pause the sim, step it exactly, and
look at the numbers (§7.5).

Numbers that matter if you go with A, all verified:

- **A body starts STATIC with gravity off — dynamics is opt-in** (`p->SetStatic(false)`,
  `p->SetGravityEnabled(true)`).
- **`AddBoxCollider` and `AddCapsuleCollider` set linear and angular damping to 0.5 and box
  friction to 1.0** behind your back (`core/physics/Physics.cpp:164-179`, `:227-240`). A ball
  built on a box collider is dragged to a halt and grabbed by every wall it touches.
  `AddSphereCollider` sets neither (`:183`). There is no damping setter on `Physics`; reach the
  raw body with `object->GetRigidBody()->setLinearDamping(0.0f)`.
- `SetBounciness(1.0f)` is as elastic as rp3d gets, and it is still not perfectly elastic. A ball
  that must not lose energy needs its speed re-normalised each tick — which is what real games do
  anyway, so that the ball never becomes slow enough to be boring.
- Contacts are reported from inside `PhysicsWorld::Update` **before the solver runs**, and the
  same contact is re-reported as `ContactStay` for every tick the bodies remain touching. Read
  velocities only on `ContactStart`; by `ContactStay` the bounce has already happened. See the
  comment at `ApplicationShip.cpp:1584` for the bug this caused there.
- **Never create or destroy a body from inside a contact callback** — rp3d is mid-iteration over
  its own arrays. Stage the intent in a member vector and act on it in `RunSimulationTick`. Both
  Ship and Tetris do exactly this.

### Bricks

A brick is an `Object` with a mesh, a material slot and a position. You have two workable shapes:

- **A fixed grid of objects, created once, hidden when dead.** 200 hidden cubes cost nothing, the
  view can be rebuilt from the rules array every tick with no incremental update to get wrong, and
  nothing churns the renderer's object list. This is what Tetris does and it removes a whole class
  of bug.
- **Created and destroyed as the game runs.** Legitimate, but see §9 on `Destroy()` — it only
  *marks*, and the actual delete happens in `Scene::DeleteDestroyedObjects()`, which most apps
  never call. Forget it and every dead brick leaves its rigid body in the physics world for the
  life of the run.

**Hang your own data on an object by subclassing it.** There is deliberately no `void* user_data`.
An asset can be loaded straight *into* an object you already made, and the reverse lookup is a
cast rather than a search:

```cpp
class Brick : public virtual Object{
public:
    int2 grid;
    int  hits_left = 1;
};

Brick* brick = new Brick();
assetmanager->GetObjectFromAsset("brick",brick);   //optional: only if it comes from an asset
main_scene->AddObject(brick);
...
Brick* b = dynamic_cast<Brick*>(hovered_object);
```

`ShipCharacter`, `Asteroid`, `HingedDoor`, `Pickup`, `IsoCell` and `IsoWall` all work this way.
The reasoning is at the top of `core/Object.h` and is worth reading before you argue with it.

---

## 6. The custom shader — Deliverable 2 in detail

### What the mechanism is

The renderer has a dedicated pass for materials that are not the standard lit surface:
`Renderer::CustomShaderPass` (`core/Renderer.cpp:487`). It runs **last of the geometry passes**,
after the solid and skinned meshes, one sub-pass per registered shader, with the deferred
G-buffer bound as texture input.

Wiring one up is four lines. This is `ApplicationIsoAnimation.cpp:102-106` and `:377`, the
smallest live example in the repo:

```cpp
//In Init(), on the render thread:
indicator_shader = new Shader("shaders/default.vert","shaders/custom.frag");
indicator_shader->uniform_callback = std::bind(&ApplicationIsoAnimation::SetCharacterUniforms,this);
int index = renderer->AddCustomShader(indicator_shader);
...
//And on the mesh that should be drawn with it:
plane->GetMesh()->mesh_mode = MESH_MODE_SHADER;
plane->GetMesh()->custom_shader_index = index;
```

`shaders/custom.frag` is that example's shader — a ~40-line arc/donut decal driven by two
uniforms. **Read it first**; it is the shortest complete answer to "what does one of these look
like". `shaders/raymarch_volume.frag` with `ApplicationShip.cpp:437-455` and `:548-549` is the
elaborate end of the same mechanism.

**Reuse `shaders/default.vert` as the vertex stage.** Every custom shader in the repo does, and
§9 explains why writing your own is a trap with a sharp edge.

### What the pass hands you, for free

From `default.vert`, as fragment inputs — the layout locations matter, copy them exactly:

```glsl
layout (location = 0) in vec3 vposition;   //fragment position in WORLD space
layout (location = 1) in vec3 vnormal;
layout (location = 2) in vec2 vuv;
layout (location = 3) in mat3 TBN;
layout (location = 6) flat in int vmatindex;
layout (location = 7) flat in int vobjid;
layout (location = 8) in vec4 vshadow;     //position in the sun's shadow space
```

Uniforms the renderer sets on every registered custom shader, every frame, before your callback:

- `mat4 mat_worldcam` — the camera matrix. **Required to exist and be used** (see §9).
- `vec3 eye_position` — the camera position, for anything that reconstructs a view ray.

Texture units bound for you (`core/Renderer.h`, the `TEXUNIT_GBUFFER_*` defines):

| Unit | Contents |
|---|---|
| 1 | G-buffer depth |
| 2 | G-buffer world position |
| 3 | G-buffer normal |

These are how a translucent material knows how far away the solid scene behind it is — a
raymarcher needs it to stop the march, and a soft-edged effect needs it to fade against geometry
instead of cutting into it. `layout(binding = 2) uniform sampler2D gbuffer_position;` and sample
with `gl_FragCoord.xy / textureSize(...)`.

SSBOs, bound globally and readable if you declare the matching blocks (copy them verbatim from
`shaders/custom.frag`, which already has all three):

| Binding | Contents |
|---|---|
| 0 | per-instance data — the object's transform, material slots, morph factors, object id |
| 1 | every material |
| 2 | every active light — position, direction, colour, brightness, `cos_angle` |

So a custom shader *can* light itself from the scene's real lights. Nothing forces it to.

**Blending is enabled globally** with `GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA`
(`Renderer::SetOpenGLState`), so writing an alpha less than 1 does what you expect, and the pass
running last is what makes that useful.

Your `uniform_callback` fires with the shader already bound, once per sub-pass per frame, on the
render thread. This is where the game's state reaches the GPU:

```cpp
void ApplicationBreakout::SetShieldUniforms(void){
    if (!shield_shader){ return; }
    shield_shader->Setfloat("charge",shield_charge);        //0..1, from the last tick's state
    shield_shader->Setfloat("time_seconds",shield_time);
    shield_shader->Setvec3("impact_point",last_impact);
}
```

Read that state from a plain member the tick wrote, not by locking anything. `DrawImGuiUI` and the
whole render thread already hold `physics_mutex` while the frame is drawn, so a member written by
`RunSimulationTick` is safe to read here.

### What the pass costs you

Worth knowing before you design around it:

- **A `MESH_MODE_SHADER` mesh does not go through the deferred pass at all** (deliberately —
  `core/Renderer.cpp:429`). So it writes no object id and **cannot be picked**, it contributes no
  position or normal to the G-buffer, and it does not occlude itself.
- **It does not cast a shadow.** The depth passes draw `MESH_MODE_NORMAL` (and skinned) only.
- **Instances of one custom mesh are not depth-sorted.** Several objects sharing a custom mesh are
  one draw call in instance order, so two that overlap on screen blend in the wrong order. Keep
  them apart, or give them separate meshes and order them yourself.
- **Your callback may change cull face, depth mask and the depth test**; all three are restored
  after each sub-pass, so nothing leaks into the next one.
- **`mat_shadow` is never set on a custom shader**, so `vshadow` is meaningless there unless you
  set the matrix yourself.

### Ideas

The requirement is one effect, done well and doing real work. Some directions that fit a paddle
game and that this pass is actually good at — pick one, or something better:

- **A force-field wall.** A quad across the top or sides that is invisible until the ball strikes
  it, then a ripple spreading from the impact point, fading over a second. Uniforms: impact point,
  impact tick, intensity. Uses the G-buffer to fade where geometry intersects it.
- **The ball's energy field.** A sphere slightly larger than the ball, drawn in the custom pass,
  whose colour and turbulence track speed, whose intensity spikes on each bounce, and which pulses
  as a power-up runs out.
- **Brick dissolve.** Each brick carries a "damage" value; the shader eats it away from a noise
  threshold, edges glowing hot, instead of the brick simply vanishing.
- **A live background.** Not wallpaper: a plane behind the play field that reacts — flowing on
  combo, flashing on a clear, darkening as lives run out.
- **The paddle's shield.** A curved surface above the paddle that charges visibly, and whose
  charge is a real gameplay resource.

Whatever you choose: **say in the findings what the pass made easy, what it made hard, and what
you had to give up.** A custom material that could not have a shadow, or could not be picked, or
needed a uniform there was no clean way to get to it, is exactly the kind of thing this run exists
to surface.

### Shader hot-reload is worth twenty minutes

`ApplicationShip::ReloadVolumeShader` (`ApplicationShip.cpp:435-455`) is the pattern: build a new
`Shader`, and if it compiled, swap it in at the index the mesh already points at —
`renderer->custom_shaders.at(my_index) = reloaded;`. **Do not call `AddCustomShader` again** on a
reload; it appends, and your mesh keeps drawing with the stale one. Bind it to a key or an ImGui
button. Iterating on a shader through full rebuild-and-restart cycles is miserable, and the engine
also has a `FileWatcher` if you want it automatic.

GLSL has a working `#include` here (`core/Shader.cpp:20-38`): a line whose first token is
`#include "path"`, resolved relative to the including file, nested up to 8 deep. The included file
must **not** carry its own `#version`. Line numbers in compile errors are remapped correctly.

---

## 7. The API surface

Everything below was read at the source. Headers in this repo are the documentation — they are
long, they explain their reasoning, and they are usually right. Read the header before you guess.

### 7.1 Scene and Object

```cpp
main_scene->AddObject(object);              // takes ownership; children come along
main_scene->FindObject("name");             // depth-first over the whole tree
main_scene->FindObjectByID(id);
main_scene->ForEachObject([](Object* o){ ... });
```

`Object`: `SetPosition/GetPosition`, `SetRotation/GetRotation` (quaternions), `SetScale`,
`MoveBy`, `RotateAroundAxis`, `YawBy/PitchBy/RollBy`, `SetLookAt`, `GetForward/GetUp/GetLeft`,
`SetVisibility`, `SetPickability`, `Destroy`, `AttachChild`/`DetachChild`/`DetachChildToWorld`,
`GetWorldPosition`/`GetWorldRotation`.

Two rules from `core/Object.h` that are enforced with `debug->Fatal`, i.e. they exit the process:

- **A physics object cannot be a child.** `UpdatePhysicsState` writes the body's *world* transform
  into the object's *local* state, so a parent transform would be applied on top of a position
  that is already final. Give the body to the parent and leave children visual-only, or use
  `DetachChildToWorld`.
- Setters are for the physics thread, or for the render thread while it holds `physics_mutex`
  (which is all of `DrawImGuiUI`).

### 7.2 Meshes

**`core/Primitives.h` is the fast path** and covers most of a paddle game:

```cpp
Mesh* MakeBox(const vec3& size);                                   // full extents, centred
Mesh* MakeQuad(float width, float height);                         // XY plane facing +Z
Mesh* MakeSphere(float radius, int segments = 24, int rings = 12);
Mesh* MakeCylinder(float radius, float height, int segments = 24, bool caps = true);
Mesh* MakeCone(float radius, float height, int segments = 24, bool cap = true);
```

All centred on the origin, sizes are full extents (`radius` is still a radius), `+Y` is the axis
of revolution, wound counter-clockwise seen from outside. That last one is why the default
back-face culling behaves, and it is what makes a box usable as the inside of a volume too.
**Render thread only** — they end in `glNamedBufferData`.

A mesh is shared by pointer and reference-counted. If you build one in `Init()` and hand it to
many objects, take a reference for your own pointer (`mesh->num_references++`), or destroying the
last object frees the mesh out from under the next one you were about to create.

Assets from `.glb`/`.obj`, if you want modelled geometry:

```cpp
gltfloader.LoadGLTFFile("data/whatever.glb");
GetAssetsFromGLTF("brick","paddle");        // or GetAllAssetsFromGLTF()
Object* o = assetmanager->GetObjectFromAsset("brick");
```

**Asset meshes are shared between every object using them**, so tagging one `MESH_MODE_SHADER`
affects all of them. Generate a fresh mesh for anything you intend to tag.

### 7.3 Materials and colour

```cpp
Material m;
m.name = "brick_red";
m.glsl_material.color     = vec4(0.90f,0.18f,0.22f,1.0f);
m.glsl_material.metallic  = 0.15f;
m.glsl_material.roughness = 0.45f;
//emissive.w is what actually makes something glow: emissive.rgb alone is clamped to 1 and so can
//never be brighter than a fully lit white surface. See core/Material.h.
m.glsl_material.emissive  = vec4(0.9f,0.18f,0.22f,0.25f);
int index = renderer->AddMaterial(m);
```

`AddMaterial` returns the index of an existing material if the name is taken, and the new index
otherwise (`core/Renderer.cpp:1382`) — that is correct now; it was not always, so ignore any
comment in the tree claiming otherwise. `FindMaterialIndex(name)` looks one up.

An object says what each of its 4 slots holds **either by name or by index, never both**:
`SetMaterialSlot(slot,index)` (an index you supply is the answer) or `SetMaterialName(slot,name)`
(resolved on the next frame once the global list is complete). Setting either cancels the other.
`SetMaterialSlot(0,-1)` means *no material at all*, which is what a custom-shader object that
computes its own colour wants.

`brightness` above 1 scales the light a surface *reflects* — crank it on an unlit object and it
stays black. `emissive` is light the surface makes on its own.

### 7.4 Input

Define your actions from `INPUT_LAST` upwards, the same convention every app uses:

```cpp
#define INPUT_BREAKOUT_LEFT     INPUT_LAST+1
#define INPUT_BREAKOUT_RIGHT    INPUT_LAST+2
#define INPUT_BREAKOUT_LAUNCH   INPUT_LAST+3
```

Map them in `Init()`. Several keys may drive one action and one key several actions:

```cpp
InputController* input = main_scene->inputcontroller;
input->AddKeyMap(VK_LEFT,INPUT_BREAKOUT_LEFT);
input->AddKeyMap('A',INPUT_BREAKOUT_LEFT);              //both, on one action, is fine
input->AddKeyMap(VK_SPACE,INPUT_BREAKOUT_LAUNCH);
input->AddKeyMap('P',INPUT_PAUSE);                      //INPUT_PAUSE is handled by the engine
input->AddGamePadMap(0,INPUT_BREAKOUT_STEER);           //analog index 0 = left stick X
input->AddKeyMap(GAMEPAD_KEY_DPAD_LEFT,INPUT_BREAKOUT_LEFT);   //pad buttons are synthetic keycodes
```

Read them in `RunSimulationTick`:

```cpp
input->IsKeyDown(INPUT_BREAKOUT_LEFT);        // held
input->WasKeyReleased(INPUT_BREAKOUT_LAUNCH); // edge, this tick
input->GetAxis(INPUT_BREAKOUT_STEER);         // scalar axis, 0 when nothing drives it
input->GetNormalizedAnalogValue(INPUT_BREAKOUT_STEER);   // gamepad analog
```

A paddle is one of the few classic games where an **analog** control is genuinely better than a
digital one, and `INPUT_EVENT_AXIS_SCALAR` / `GetAxis` exist. A mouse-driven paddle is also very
much in the spirit of the genre — `INPUT_MOUSE_DELTA_X` is raw, unaccelerated, unclipped movement
that keeps reporting when the pointer hits the edge of the screen, which is exactly what you want
and is *not* what `INPUT_MOUSE_X` gives you.

**The focus gate.** Input is not sampled while another application is in the foreground
(`input->HasFocus()`), which is right for a human and wrong for a script — an MCP-driven test runs
precisely when your window is *not* in front. Scripted holds are deliberately not focus-gated, so
the correct predicate for "should I act on input" is:

```cpp
if (!input->HasFocus() && !input->HasSyntheticHolds()){
    return;     //someone else's keystrokes; not ours
}
```

`HasSyntheticHolds()` deliberately stays true for the one extra tick that carries a scripted
release, so edge-triggered actions survive. Every app writes this gate by hand and the engine
still has no single predicate for it (backlog item 15) — if that costs you, say so.

### 7.5 The tick, pausing and stepping

```cpp
main_scene->GetPhysicsTick();      // ticks that have actually RUN. THE clock.
main_scene->GetPhysicsTimestep();  // fixed, in seconds
SetPhysicsTPS(60.0f);              // from Init(), if 50 is the wrong rate for your game
```

**Every duration in your simulation is a count of ticks.** Not `GetTickCount64()`, not
milliseconds. Real time is ~15.6 ms-granular against a 20 ms tick, it keeps running while the
simulation is paused or single-stepped, and a wall-clock duration therefore means something
different on every run. A ball speed-up timer, a power-up duration, a respawn delay, a flash: all
of them are `ticks_remaining`.

This is also what makes the game *measurable*, which is the whole reason to care:

```
sim_pause {"paused": true}          # freeze the simulation; the render loop keeps running,
                                    #   so the window stays responsive and screenshots still work
sim_step  {"num_ticks": 48}         # advance by EXACTLY 48 whole ticks - input, animation,
                                    #   your RunSimulationTick, then physics
```

Check `ticks_advanced` in the reply, not `num_ticks`: short means the call timed out and the rest
is still queued, and a caller that assumes otherwise reads the wrong state.

**Pause before you measure.** An MCP tool handler holds no lock, so reading a free-running
simulation is a straight data race against the physics thread.

### 7.6 Physics

```cpp
Physics* p = object->AddPhysics(main_scene->physics_world);
p->AddBoxCollider(half_extents, offset, quat().identity(), density);
p->AddSphereCollider(radius, offset, quat().identity(), density);
p->SetStatic(false);            // bodies start STATIC with gravity OFF
p->SetGravityEnabled(true);
p->SetBounciness(0.9f);
p->SetFrictionCoefficient(0.1f);
p->SetVelocity(v);  p->SetAngularVelocity(w);
p->AddWorldForceAt(force,point);  p->AddLocalTorque(t);
p->SetTrigger(true);            // overlap reporting without a solid response
object->GetRigidBody();         // raw rp3d body, for anything the wrapper doesn't expose
main_scene->physics_world->Raycast(from,to,exclude_body);
```

Collision filtering: `SetCollisionCategoryBits` / `SetCollideWithMaskBits` on the object.

Contacts, if you want them — inherit `rp3d::EventListener` alongside `Application`, and
`main_scene->physics_world->rp_world->setEventListener(this)` after the bodies exist:

```cpp
class ApplicationBreakout : public Application, public rp3d::EventListener{
    void onContact(const rp3d::CollisionCallback::CallbackData& data) override;
    void onTrigger(const rp3d::OverlapCallback::CallbackData& data) override;
};
```

`contactPair.getBody1()->getUserData()` is the `Object*`. Filter on
`EventType::ContactStart` unless you genuinely want every tick of a resting contact — see the
warning in §5. `ApplicationShip.cpp:1536` and `ApplicationDozer.cpp:663` are the two worked
examples.

### 7.7 Sound

```cpp
soundsystem = new SoundSystem();
soundsystem->Initialise();
soundsystem->AppendFile("data/sound/click.wav","bounce_wall");
soundsystem->AppendFile("data/sound/click.wav","bounce_paddle");   //same file, second handle
soundsystem->AppendFile("data/sound/bleep.wav","brick");
soundsystem->AppendFile("data/sound/floop.wav","clear");
soundsystem->AppendFile("data/sound/hax.wav","lost");
...
soundsystem->Play("brick",false,0.8f);
```

`data/sound/` has four WAVs: `bleep`, `click`, `floop`, `hax`. That is what exists; making a game
sound good with four samples, pitch aside, is part of the exercise — or add your own.

Two things that will bite a paddle game specifically, because it makes noise *fast*:

- **Each handle gets its own OpenAL source and a source cannot overlap itself.** Two bricks broken
  on the same tick through one handle is one sound. Registering the same file under several
  handles and round-robining them is the workaround, and it is what Tetris does.
- **`NUM_AL_BUFFERS` is 16**, so there are sixteen handles in total for the whole app.

Sound must be triggered from the tick (it is a consequence of simulation), and it will therefore
also fire while you single-step. That is correct, if slightly startling.

### 7.8 Motion over ticks

Interpolated movement without writing a lerp:

```cpp
//Replaces any motion in flight for this object. Position and/or rotation; NULL leaves one alone.
main_scene->MoveObjectOverTicks(object,&target_pos,NULL,12);
//APPENDS instead: this leg starts where the last one ended. A there-and-back is Queue, Queue.
main_scene->QueueObjectMotion(object,&out_pos,NULL,6);
main_scene->QueueObjectMotion(object,&home_pos,NULL,6);
```

Exactly `ticks` ticks for an object without physics; `ticks + 1` for one with a body (the last
tick sets a velocity the solver has not integrated yet). An object with a body is switched to
KINEMATIC for the duration and driven by velocity, so it shoves dynamic bodies out of the way
properly instead of teleporting through them.

**Re-requesting the same motion every tick restarts it every tick and it never finishes.** Only
call it when the target actually changes. This is a real trap and it looks exactly like "my
object refuses to move".

### 7.9 Text in the world

`core/TextMesh.h` bakes a string into a single `Mesh` of extruded glyphs — an ordinary object,
lit and shadowed like anything else, and it appears in screenshots.

```cpp
GlyphSet glyphs;
LoadGlyphSetFromGLB(glyphs,"shared_assets/meshes/glyphs_unispace.glb",0.509167f,1.0f);   //advance, line_height
TextLayout layout; layout.scale = 0.8f; layout.align = TEXT_ALIGN_CENTER; layout.matid = 0;
score_mesh = BuildTextMesh(glyphs,"SCORE 1200",layout,score_mesh);       //reuse, don't reallocate
```

- One world unit per line at scale 1; origin is the left end of the first baseline.
- **`BuildTextMesh` is render thread only.** Rebuild labels in `PreRender`, from state the tick
  published — not in the tick itself.
- **`MeasureText` is the exception**: pure maths, safe anywhere, so the simulation can decide
  *where* a label goes on the tick the score changes.
- **Pass `reuse`.** `Mesh` has no destructor, so rebuilding a score label by delete/new leaks a
  VBO and a VAO per point scored.
- Printable ASCII only, monospaced, no atlas and no font library involved.

### 7.10 Changing the simulation from another thread

Two queues, and which one you need depends on which direction you are going.

**Writing — `SimCommand`.** Anything that changes the simulation and is not input: spawn,
teleport, destroy, restart. It is a plain trivially-copyable struct (so it can one day be recorded
and replayed), dispatched to a handler you register:

```cpp
#define BREAKOUT_CMD_RESTART  SIM_CMD_LAST+0

main_scene->RegisterCommandHandler(BREAKOUT_CMD_RESTART,
    [this](const SimCommand& cmd) -> objectid_t {
        NewGame((uint32_t)cmd.value[0]);      //the seed travels IN the command
        return OBJECTID_INVALID;
    });
```

The handler runs on the physics thread, at the top of a tick, with the mutex held — the only place
it is safe to throw the world away. Submit with `main_scene->SubmitCommand(cmd)` from any thread,
`Application::SubmitCommandAndWait(cmd)` from an MCP thread that needs the result, and
`Application::SubmitUICommand(cmd)` **from the debug UI, which must never wait** — `DrawImGuiUI`
holds the mutex the physics thread needs, so waiting there deadlocks instantly.

**Reading — `Scene::AtTickBoundary(fn)`.** Runs `fn` with the simulation held still, between
ticks:

```cpp
main_scene->AtTickBoundary([&](){ snapshot = ...; });
```

Two rules, both deadlocks if broken: `fn` must not wait on the physics thread (no
`SubmitCommandAndWait`, no `StepPhysicsAndWait`), and must not wait on the render thread (no
screenshot — the render thread takes the same mutex to draw). Keep it short; the simulation is
stopped for as long as it runs.

**Or publish a snapshot.** For state that every tool call reads, filling a struct under a small
mutex at the end of each tick and serving reads from it costs one copy per tick and never disturbs
the game it is measuring. Tetris does this; both approaches are correct and the choice is
situational.

### 7.11 MCP — how the game gets played without a human

Every app embeds an MCP server on **`http://127.0.0.1:8765/mcp`**.

**Use `127.0.0.1`, never `localhost`.** The server binds IPv4 only, deliberately. Where
`localhost` resolves to `::1` first, every call pays a failed IPv6 connect — measured at 2,058 ms
against 15 ms. Everything still works, 137x slower, with nothing appearing wrong.

Tools every app gets for free: `status`, `object_list`, `object_get`, `object_set_transform`,
`object_move`, `object_spawn`, `sim_pause`, `sim_step`, `sim_command`, `asset_list`,
`camera_get`, `camera_set`, `screenshot`. (Destroying an object has no tool of its own; it is a
`SIM_CMD_OBJECT_DESTROY` through `sim_command`, which is also how you reach your own command
types from outside.)

`screenshot` returns a PNG and is the fastest way to check visual work. **It captures ImGui by
default now** (`include_ui`, default true) — pass `include_ui: false` for the clean 3D scene. Any
stale note saying otherwise is out of date.

**Register your own tools from `Init()`**, which is guaranteed to be before the server accepts
requests:

```cpp
MCPServer::Get()->RegisterTool("breakout_state",
    "Board, ball, paddle, score and lives as of the last simulated tick.",
    json{{"type","object"},{"properties",json::object()}},
    [this](const json& args) -> json { return BuildStateJson(); });
```

**Build enough tools that you can play and verify the game entirely through them.** At minimum:
one that reads the whole game state, and one that drives the paddle and launches the ball. The
scripted-input path is `input->HoldKey(mapped, duration_ticks)` and
`input->HoldAxis(mapped, value, duration_ticks)`, whose durations are **simulation ticks**, so a
scripted press behaves identically whether the game is running, single-stepped or replayed.

This is not busywork. It is how you check a bounce angle is what you intended rather than what it
looks like, how you run the ball for 10,000 ticks looking for a tunnelling event, and how you
prove the game still works after a refactor. `tools/tetris_bot.py` and `tools/vehicle_mcp.py` are
two existing scripted drivers to crib the shape from.

### 7.12 The HUD

`DrawImGuiUI()` on the render thread, mutex held. `Application::RenderApplicationUI()` draws the
engine's own dockspace (Scene / Inspector / Engine panels); call it or do not. Tetris hides it
behind F1 so that the game screen is the game, and that is a good default for a game app.

Your own panel is ordinary ImGui. Anything in it that **changes** the simulation must go through
`SubmitUICommand` (never `SubmitCommandAndWait`) — see §7.10.

Note the engine's panels dock to the **left**, so anything you place in world space on that side
will be covered.

### 7.13 Lights and shadows

`DirectionalLight` (casts the sun shadow map; is also a `Camera`, hence `viewport.zoom`),
`PointLight`, `ConeLight`. All are `Object`s: add them to the scene, move them, select them in the
Inspector. `color`, `brightness`, `f_casts_shadow`.

Point lights have no shadow map of their own, but there is an **occluder field**
(`Renderer::EnableFieldShadows(camera, axis, size)`): one top-down texture holding, per world
column, the highest and lowest surface plus a jump-flooded distance, which the surface shader
sphere-traces to shadow point lights without a cube map. `ApplicationTetris::SetupFieldShadows` is
the only user and is the example to copy. Its limitations are documented as backlog items 39-43 —
read them before you rely on it; notably a column is a single slab, so a ball *above* a brick
merges with it and light cannot pass between the two.

A moving point light on the ball is a cheap, striking effect in a game this dark, and it is two
lines. Whether the field shadow behaves under it is an interesting thing to find out and report.

---

## 8. Suggested order of work

Get to "it is a game" early and keep it working. Every step below leaves something runnable.

1. **Skeleton.** `apps/Breakout.mk`, the class, `Init()` from §4, a camera and a light. Build, run,
   see a lit cube. Start `docs/breakout_findings.md` in the same commit.
2. **The rules, headless.** `breakout/` — the brick grid, the ball's position and velocity, the
   paddle, the collision resolution, score and lives, as plain C++ with no engine types. This is
   where the game actually lives and it is the part worth getting right before anything is drawn.
3. **The view.** One object per brick, a ball, a paddle, walls. Drive it all from the rules each
   tick. Now it is playable.
4. **Input and feel.** Keys, then the analog or mouse control. Tune the paddle's response and the
   bounce angle. This is the step that decides whether the game is any fun; give it real time, and
   use `sim_pause`/`sim_step` to measure rather than guess.
5. **MCP tools.** State out, control in. From here on you can test without your hands.
6. **The custom shader.** Deliverable 2. Do not leave it to the end — it is the part most likely
   to surface something interesting about the engine, and it wants iteration. Hot-reload first.
7. **Sound.** Cheap, and it transforms how the game feels.
8. **Physics, deliberately.** Whatever you decided in §5 — debris, falling bricks, a physical
   power-up. Something that is genuinely better for being simulated.
9. **Score, lives, levels, game over, restart.** Text in the world (§7.9) rather than only ImGui,
   so it survives a screenshot.
10. **Polish and the report.** Camera shake on a big clear, a launch sequence, whatever the game
    is asking for. Then read your findings through as a document rather than as notes.

---

## 9. Known traps, verified

These are real, they are in the tree right now, and each one costs at least half an hour when met
cold.

**Shaders**

- **A GLSL compile or link error calls `debug->Fatal`, which calls `exit(1)`**
  (`core/Shader.cpp:175`, `:205`, `:235`, `:273`). A typo in a shader does not log a warning and
  carry on — the application vanishes. The message is on stderr; read `wind_stderr.log`.
- **`Shader::Setmat4` and `Setmat3` on a missing uniform also call `Fatal`** (`:352`, `:361`).
  `Setint` logs an error and `Setfloat`/`Setvec3` log a warning, which is an inconsistency worth
  noting in itself. The consequence: `CustomShaderPass` calls `Setmat4("mat_worldcam", ...)` on
  **every** registered custom shader every frame, so **a custom shader whose program does not have
  a live `mat_worldcam` uniform kills the app on the first frame**. GLSL optimises away a uniform
  that is declared but unused, so "I declared it" is not enough — it must actually be used. Using
  the stock `shaders/default.vert` as your vertex stage satisfies this for free, which is the real
  reason every custom shader in the repo does.
- Uniform setters name their own program (`glProgramUniform*`), so they no longer depend on bind
  order. `Use()` is needed before drawing, not before setting.

**Objects and the scene**

- **`Object::Destroy()` only marks.** The actual delete happens in
  `Scene::DeleteDestroyedObjects()`, which only Dozer, Ship and
  Tetris call. In every other app a destroyed object stops rendering but **its rigid body stays in
  the physics world forever**. If you destroy bricks, call it — from `RunSimulationTick`, where
  the mutex is held and the render thread is not walking the object list. This is open backlog
  item 23.
- **`Scene::AddObject` from inside an `UpdatePhysicsState` override** `push_back`s the vector
  `Scene::UpdatePhysics` is iterating. Stage it and add from `RunSimulationTick`.
- **Never create or destroy a physics body from inside a contact callback.** Stage it.
- A physics object cannot be a child (see §7.1) — it is a `Fatal`, not a warning.

**Timing**

- **`MoveObjectOverTicks` re-requested every tick never completes.** Track the last target you
  asked for and only re-request on a change.
- A motion on an object **with** a body takes `ticks + 1`; without one, exactly `ticks`.
- Anything you denominate in real milliseconds is wrong in a way that only shows up when the game
  is paused, stepped or run on another machine.

**Randomness**

- `RRandom` can now be seeded per instance (`new RRandom(seed)`), and the recommended shape is
  **one instance per game, created once in `Init()`**. It is not thread safe by design.
- **A draw from the UI or an MCP thread shifts the stream for the simulation**, regardless of the
  seed (open backlog item, "rrand shared stream"). If your game's randomness must be reproducible,
  draw only from the tick.

**Sound** — see §7.7: one source per handle, 16 handles total.

**Other**

- `GLTFLoader` produces `inf`/`NaN` tangents for any mesh with degenerate UVs — 62% of the
  triangles in `shared_assets/meshes/glyphs_unispace.glb`. Harmless until something normal-maps such a mesh, at
  which point the symptom points nowhere near the loader. Open backlog item 38.
- Several apps gate keyboard control on `ImGui::GetIO().WantCaptureMouse` for historical reasons.
  A paddle that stops because the cursor drifted over a panel is a bad time; decide deliberately.
- `Camera::SetupPerspective`/`SetupOrthographic`'s `width`/`height` are overwritten every frame by
  `Renderer::DrawFrame`. Pass the real dimensions anyway — `GetPixelRay` reads them before the
  first frame.

---

## 10. What to hand back

`docs/breakout_findings.md`, in this structure:

1. **Summary.** Is there a playable paddle game, and what is the honest state of it? What did you
   cut, and why?
2. **The custom shader.** What the effect is, how it is wired, what the custom-material pass made
   easy, what it made hard, and what you had to give up to use it. Include the shader's own story:
   what you tried first, what did not work, how long the iteration loop was.
3. **What the engine did well.** Genuinely — which APIs were a pleasure, what took one line that
   would have taken fifty elsewhere. This is as useful to the author as the complaints, and it is
   the part agents skip.
4. **Missing features**, ranked by what they cost you. For each: what you wanted to do, what you
   did instead, and a sketch of what the API should have looked like.
5. **Bugs found.** File, line, reproduction, expected vs actual. Say clearly whether you confirmed
   each one yourself or inferred it from reading. Check `docs/engine_backlog.md` first so you can
   say whether something is already known — a *new* find is worth flagging as new.
6. **Changes to `core/`.** One entry per change, with the justification and what it would have
   cost you not to make it. Required, and it must be complete.
7. **Friction and papercuts.** Boilerplate, wrong comments, confusing names, things you got wrong
   twice. Small things belong here; the pattern across them is the finding.
8. **The design decisions you made and why** — the ball question from §5 above all, but also the
   camera, the object-per-brick choice, and anything else where the engine pushed you one way.
9. **What you would build next** with another day, both in the game and in the engine.

And in the repo itself: the app, its folder, its shader(s), its `apps/*.mk`, and any scripted
driver you wrote under `tools/`. Leave the tree building for all twelve apps.
