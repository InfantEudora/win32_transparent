# ReactPhysics3D: where the binary size goes, and what actually removes it

Investigated 2026-09-13/14 against `C:/code/reactphysics3d` on branch `vehicle-constraint`,
built the way we build it: MSYS2 MINGW64, GCC 13.1.0, GNU ld 2.40, default Release
(`-O3 -DNDEBUG`), statically linked.

Result: a **49% smaller binary** for the physics side, from removing a dependency that had
nothing to do with physics. Work is committed on branch `size/no-iostream-dependency`
(branched off `vehicle-constraint`). The game already builds and runs against it.

Two CMake options, both defaulting to **ON** so nothing changes unless you ask:

| option | effect when OFF |
|---|---|
| `RP3D_COMPILE_DEFAULT_LOGGER` | drops the built-in `DefaultLogger` (the `<fstream>`/`<sstream>` user) |
| `RP3D_COMPILE_DEBUG_STRINGS` | drops every `to_string()` and all log message text; **forces the logger off too** |

```
  strings ON,  logger ON  (default)   2,242,048
  strings ON,  logger OFF             1,413,120
  strings OFF (forces logger OFF)     1,132,032
```

`RP3D_COMPILE_DEBUG_STRINGS=OFF` is the one to use. It subsumes the other.

## Read this first: both defines must match, or you get the 2026-09-07 bug again

Same failure class as the `VehicleWheelSettings` incident in `rp3d_brake_steer_task.md`, and
just as quiet — but one of these is worse.

**`IS_RP3D_DEFAULT_LOGGER_ENABLED` changes a struct size.**

```
sizeof(PhysicsCommon)    logger ON: 2184      logger OFF: 2120
```

64 bytes, the `Set<DefaultLogger*> mDefaultLoggers` member. Everything else is byte-identical.
Verified: a consumer compiled without the define, linked against a library built with it,
produced no error of any kind. `PhysicsCommon physicsCommon;` on the stack then reserves 2120
bytes while the library's constructor writes 2184.

**`IS_RP3D_DEBUG_STRINGS_ENABLED` renumbers vtables, which is nastier.** It adds or removes a
pure virtual `to_string()` on `CollisionShape` and `Joint`, so every virtual after that slot
shifts:

```
  strings ON   slot 14: BoxShape::to_string()
               slot 15: BoxShape::getLocalSupportPointWithoutMargin()
  strings OFF  slot 14: BoxShape::getLocalSupportPointWithoutMargin()
```

Object layout does **not** change (`sizeof(BoxShape)` is 80 and `sizeof(SliderJoint)` is 24
either way), so nothing looks wrong: it links silently and then calls through the wrong vtable
slot with the wrong signature. There is no diagnostic of any kind, at any stage.

So the rule from the other doc extends: **whenever you rebuild the library, carry both defines
across in the same step** — into the game's `3rdparty/reactphysics3d/` copy of the headers and
into the game's own compile flags. Set them on both sides or neither. CMake does this
automatically if you link the target (both are PUBLIC compile definitions); our build does not,
so it is manual.

If we ever ship a prebuilt `.a` to someone else, the logger member can be kept unconditionally
(`Set<DefaultLogger*>` only needs `DefaultLogger` forward-declared) so that layout is stable.
The vtable one cannot be made safe that way — it is inherent to removing a virtual.

## What was actually eating the binary

Attribution was done by parsing the GNU ld map and reconciling against the real executable
(agreed to within 0.04%).

```
                        before            logger OFF          strings OFF
  ReactPhysics3D    1,214,172  54%     1,181,708  83%        919,880  81%
  libstdc++/CRT       996,676  45%       206,506  14%        206,506  18%
  total             2,237,440           1,413,120          1,132,032
```

Nearly half the binary was the C++ runtime, and essentially all of that was **iostream and
locale**, not anything physics-related. Measured cost of each dependency when statically linked:

```
  empty C++ exe            16,896
  + std::string           199,680     (+183 K)   <- we use std::string anyway, so shared
  + std::to_string        202,240     (+2.5 K)   <- routes through vsnprintf, not locale
  + std::stringstream     942,592     (+743 K)   <- the whole cost, one class
```

Three independent things pulled it in, and removing any one alone saved nothing:

1. `std::stringstream` in seven `to_string()` bodies (every *other* `to_string()` in the
   library already used `std::string` + `std::to_string`, so these were converted to match).
2. Ten stray `#include <iostream>`, none of them used. Each plants a `std::ios_base::Init` in
   that translation unit, which alone drags in the locale machinery.
3. `DefaultLogger`, which is genuinely stream-based — hence the option.

Also changed: `RP3D_VERSION` from a namespace-scope `const std::string` to
`inline constexpr const char*`. As a `std::string` it gave all 94 translation units their own
static constructor and `atexit` registration.

Note `to_string()` decimals now format as `%f` (`0.300000`) rather than the ostream default
(`0.3`), consistent with the existing `Vector3`/`Quaternion`/`Transform` implementations.

## What is left, and what we deliberately did not do

Inside the library now: core physics + containers 70%, `to_string()` debug dumps 14.3%
(156 KB), mesh/hull construction 12%, DebugRenderer 4%.

Of the remaining 206 KB of runtime, the largest single item is `cp-demangle.o` at 52 KB — the
Itanium-ABI name demangler, pulled in by `__gnu_cxx::__verbose_terminate_handler` purely so an
uncaught exception prints a readable type name instead of a mangled one. Defining our own
handler displaces it (measured: −52,736 bytes, exception message still readable as
`St13runtime_error`). That belongs in the engine, not in RP3D — it is a process-wide policy
choice. Not done.

`to_string()` was the largest discretionary item at 156 KB. It is now behind
`RP3D_COMPILE_DEBUG_STRINGS`, which removes 281,088 bytes in total — the `to_string()` bodies
plus the `std::string` concatenation that built every log message at ~72 `RP3D_LOG` sites.
With it off the binary contains zero `to_string` bodies and zero stream/locale objects.

## The linker will not help: `--gc-sections` does not work here

This was the surprise, and it is worth writing down so nobody spends an afternoon on it again.

On this toolchain, `-ffunction-sections -fdata-sections -Wl,--gc-sections` buys about 5 KB,
all of it from import-library objects. **It does not remove unused function code at all.**
Verified with a minimal control — a function never called, address never taken, compiled with
`-ffunction-sections` — and it is still fully present in the binary afterwards.

The cause is not an old binutils. GCC on mingw emits `-ffunction-sections` code into PE
*grouped* sections (`.text$name`), which the format defines as pieces of one output `.text`
sorted by suffix. GNU ld collects `.rdata$` grouped sections but never `.text$` ones.

Full matrix, same source, `--gc-sections` throughout:

```
                      unused plain fn     unused COMDAT fn
  gcc   + ld 2.40          KEPT                KEPT
  gcc   + ld 2.47          KEPT                KEPT          (sandboxed, 2026 build)
  gcc   + LLD 22           KEPT              dropped
  clang + LLD 22         dropped             dropped
```

So the platform *can* dead-strip, but only via clang + LLD. On the real library that gave
1,032,704 bytes with 14 of 17 `to_string` bodies collected — but that number is flattered by a
toy probe app that touches almost none of the library, and it requires moving the whole engine
to clang + LLD, with the ABI rebuild that implies.

Two practical warnings:

- **Do not carry `-ffunction-sections` as a "size flag" on GCC + GNU ld.** It collects nothing.
- With **clang objects + GNU ld** it is actively harmful: 2,072,474 bytes, worse than every
  other combination, because the per-section alignment padding is paid without any collection.

I could not pin down the exact COFF-level reason GCC's and clang's `.text$name` sections behave
differently under LLD. Ruled out: the COMDAT flag, `.pdata` associativity, and `.llvm_addrsig`.

## Conclusion

`#ifdef` / build options are the only lever that works on our toolchain — not because we failed
to find the right linker flag, but because the sections are not collectable and the code is
reachable. A two-line preprocessor guard removes what no linker configuration will.

## Reproducing

```sh
# from a MINGW64 shell
cmake -S . -B build-nostrings -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DRP3D_COMPILE_DEBUG_STRINGS=OFF
cmake --build build-nostrings -j
```

and on the game side, compile with `-DIS_RP3D_DEBUG_STRINGS_ENABLED` **absent** — i.e. do not
define it anywhere. (With `RP3D_COMPILE_DEBUG_STRINGS=ON` you must define it on both sides.)

Unit tests pass in both configurations. The testbed needs the strings; combining it with
`RP3D_COMPILE_DEBUG_STRINGS=OFF` stops at configure time with an explicit message rather than
failing deep in the build.
