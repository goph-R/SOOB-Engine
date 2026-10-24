# Lua scripting — Plan

## Goal

Expose a tiny scripting surface so level/gameplay logic can live in `.lua`
files instead of being hardcoded in `main.cpp`. Start minimal — three
functions, one entry hook — to prove the binding works end to end before
growing the API.

First-cut C API exposed to Lua:

| Lua call | C side | Purpose |
|---|---|---|
| `ui_show_message(text [, seconds])` | new in `ui.h` | transient centered HUD message |
| `snd_play(name)` | new in `sound.h` (name registry) | fire-and-forget play of a named buffer |
| `ent_activate(target)` | existing `entActivate()` in `entity.h:152` | trigger switch/door/platform by name or group |

## Choice of Lua: 5.1.5

- **Lua 5.1.5** — plain C89, compiles clean on Dev-C++ / MinGW 3.4, ~17 KLOC,
  no OS dependencies, double-precision `lua_Number` is fine on a P4.
- Not 5.2/5.3/5.4 — later versions drift toward C99 idioms and some headers
  assume `<stdint.h>` which MinGW 3.4 lacks. Staying on 5.1 also keeps the
  API stable (`luaL_register`, `lua_objlen`) which matches the style already
  used in the engine.
- Not LuaJIT — x86-only asm, C99, ~10× the binary size, overkill.

## Vendoring + build

- Drop the Lua 5.1.5 source tarball into `vendor/lua-5.1.5/src/`.
- Create one unity file `vendor/lua-5.1.5/src/lua_all.c` that `#include`s every
  `.c` except `lua.c` (the interpreter `main`) and `luac.c` (the compiler).
  This matches the Bullet unity-build pattern: one `.o` to cache.
- Build outputs:
  - Makefile: `lua.o` via `$(CXX) -x c -c vendor/lua-5.1.5/src/lua_all.c`
    (compiled as C, linked into the C++ binary).
  - `build.bat` / `build_win10.bat`: same `if exist lua.o ...` cache pattern
    as `bl.o` / `bc.o` / `bd.o`. Delete `lua.o` when bumping Lua.
- Include path: `-Ivendor/lua-5.1.5/src`.
- Lua compile flags: define `LUA_ANSI` on Win98 (skips the `LUA_USE_POSIX`
  branches). On Linux, default flags are fine. No `-DLUA_USE_DLOPEN` —
  we don't want `require 'socket'` etc. pulling in C modules.

## New module: `script.h`

Header-only, same style as the rest of the engine.

```c
struct ScriptSystem {
    lua_State *L;
    /* borrowed pointers so bindings can reach engine state */
    UiState      *ui;
    SoundSystem  *snd;
    EntityList   *entities;
};

static int scriptInit(ScriptSystem *s, UiState *ui, SoundSystem *snd,
                      EntityList *el);         /* opens state, registers libs, registers C bindings */
static void scriptShutdown(ScriptSystem *s);
static int scriptRunFile(ScriptSystem *s, const char *path);   /* luaL_loadfile + lua_pcall */
static int scriptCall(ScriptSystem *s, const char *fn);        /* calls a global nullary fn if it exists; no-op otherwise */
```

`main.cpp` owns the `ScriptSystem`, constructs it after `ui`, `snd`, and
`entities` are ready, and calls `scriptRunFile(&script, "scripts/main.lua")`
at startup. Entry points into Lua:

- `scriptCall(&script, "on_start")` — once after level load.
- Later: `on_activate(name)`, `on_trigger(name)`, `on_frame(dt)`. Not in v1.

Errors: every `lua_pcall` wraps with `lua_pushcfunction(L, traceback)`;
on failure we print the message via `fprintf(stderr, ...)` and pop. Never
longjmp out of engine code — keep the engine authoritative.

## Binding details

### `ui_show_message(text [, seconds])`

Needs a small addition to `ui.h` — there's currently no transient message
primitive. Extend `UiState`:

```c
char  msgText[128];
float msgTimeLeft;   /* seconds; 0 means no message */
float msgTotal;      /* for fade; 0 means no message */
```

Plus two functions:

- `uiShowMessage(UiState*, const char *text, float seconds)` — copies the
  string (truncate at 127), sets timers.
- `uiUpdateMessage(UiState*, float dt)` — called each frame to decrement.
- Inside the existing per-frame `uiBegin`/`uiEnd` block in `main.cpp`, if
  `msgTimeLeft > 0` draw it centered near the top (e.g. `y = -halfH * 0.5f`)
  with alpha ramping down over the last 0.5 s.

The C binding just validates args and calls `uiShowMessage`.

### `snd_play(name)`

Sound buffers today are held as loose `SoundBuffer` locals in `main.cpp`
(`createTone`, `createGunshot`, `createFootstep`). For Lua to address them
we need a name→buffer registry. Add to `sound.h`:

```c
#define SND_MAX_NAMED 64
struct SoundLibrary {
    char        names[SND_MAX_NAMED][32];
    SoundBuffer bufs [SND_MAX_NAMED];
    int         count;
};
static void        sndLibInit(SoundLibrary *lib);
static void        sndLibRegister(SoundLibrary *lib, const char *name, SoundBuffer b);
static SoundBuffer sndLibFind(SoundLibrary *lib, const char *name);  /* 0 if not found */
```

`main.cpp` registers `"gunshot"`, `"footstep"`, etc. after creating them,
then `sndPlay` binding does `sndPlay(s->snd, sndLibFind(&s->lib, name))`.
`ScriptSystem` borrows the library pointer the same way it borrows the UI.

The binding logs a warning for unknown names but doesn't raise a Lua
error — easier for content authors to iterate.

### `ent_activate(target)`

`entActivate(EntityList*, const char*)` already does exactly the right
thing (entity.h:152) — matches by name OR group, cascades through switch
targets. The binding is one line.

Return value: the existing C function is void. For v1 we keep it void
(Lua returns nothing). If scripts ever need to know whether anything
matched, change the C side to return an `int count` and propagate.

## When does Lua run?

v1: **only `on_start()`**, called once right after level + entities load.
Enough to script intro cutscenes, starting messages, demo sequences.

Deliberately out of scope for v1:
- Per-frame callbacks (`on_frame(dt)`) — easy to add, but no use case yet.
- Trigger/door/switch callbacks — would require threading script hooks
  through entity.h. Worth doing, but as a second pass once the binding
  is proven.
- Coroutines / `wait(seconds)` — the natural way to script timed
  sequences. Add after frame callbacks exist.

## `scripts/main.lua` — minimum viable example

```lua
function on_start()
    ui_show_message("Welcome to SOOB-Engine", 3)
    snd_play("gunshot")
    ent_activate("intro_door")   -- opens a door tagged intro_door in .ent
end
```

Success criterion for the first milestone: the three lines above run at
level start on both Linux and Win98 and produce the expected effects.

## Risks / open questions

- **MinGW 3.4 + Lua 5.1 headers.** `luaconf.h` has some GCC version checks
  that may not match 3.4. If the unity build errors, patch `luaconf.h` in
  place and note it in `docs/plan-lua.md`. Don't touch other Lua files —
  we want `lua_all.c` to stay a drop-in.
- **Memory.** Default Lua allocator uses `realloc` — fine on modern, fine
  on Win98 too (the MinGW libc `realloc` works). Not worth writing a
  custom allocator in v1.
- **C++ linkage.** Lua is C; build with `extern "C"` around the includes
  in `script.h` (Lua's own headers already handle this, but be explicit).
- **Hot reload.** Not in v1. If we want it later, `scriptRunFile` already
  gives us the primitive — bind it to a debug key.
- **Sandboxing.** `os.execute`, `io.open`, `require`, `package.loadlib`
  should be nil'd out of `_G` after `luaL_openlibs` so a bad script
  can't delete files or load DLLs. Do this in `scriptInit` before
  running any user code.

## Milestones

1. Vendor Lua 5.1.5, get `lua.o` building on Linux + Win98 (no bindings yet).
2. `script.h` with `scriptInit`/`Shutdown`/`RunFile`/`Call`; call `on_start`
   from `main.cpp`; verify `print("hello")` reaches stdout.
3. `uiShowMessage` in `ui.h` + per-frame render in `main.cpp`.
4. `SoundLibrary` in `sound.h`; register built-in sounds by name.
5. Register the three bindings; ship the example `scripts/main.lua`.
6. Sandbox pass (nil out `os`, `io`, `package`, `require`, `dofile`, `loadfile`).
