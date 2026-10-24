# SOOB-Engine Features

What the current engine can do, by subsystem. This is the human-readable
companion to `CLAUDE.md` — that file optimizes for "what does an AI
coding assistant need to know to edit this code?"; this one optimizes
for "what does the engine actually do?"

The hard targets that shape every feature below:

- Runs on **Windows 98** with Dev-C++/MinGW 3.4 + SDL 1.2 + OpenAL Soft
  1.9.563, all the way up to modern Linux/Windows.
- **GPU floor: GeForce 4 MX 440 32 MB.** DX7 class, 2 TMUs, GL 1.3
  + `GL_ARB_multitexture` + `GL_ARB_texture_env_combine`. No
  programmable shaders, ever. Untested since the reference card died
  on 2026-10-24 — the bench cards (FX 5200, Radeon 9200) both sit
  above the floor, so it has to be held by reading GL calls. See
  CLAUDE.md.
- Single-translation-unit C++ (no C++11), header-only modules. The
  three Bullet unity files and the Lua unity file are the only other
  TUs.

---

## 1. Build & run

Four build systems — pick the one matching your toolchain:

| Target | Command | Output |
|---|---|---|
| Linux | `make` (needs `libsdl1.2-dev`, `libopenal-dev`) | `soob` |
| Windows 98 / Dev-C++ | `build.bat` | `SOOB.exe` |
| Windows 10 / portable WinLibs MinGW | `build_win10.bat` | `SOOB_w10.exe` |
| CMake | `mkdir build && cd build && cmake .. && make` | `SOOB` |

The exe runs from the repo root and reads everything under `assets/`
as relative paths. `cd build && ./SOOB` will fail to find assets.

CLI flags: `-w <px>`, `-h <px>`, `-fullscreen`. Anything unrecognized is
ignored.

---

## 2. Rendering

### 2.1 Pipeline

Fixed-function OpenGL 1.3, immediate-mode (`glBegin`/`glEnd`). All
matrix math goes through GL — perspective and view matrices are built
manually in `main.cpp` and submitted via `glMultMatrixf`. Two texture
units in use throughout, multi-texturing via the ARB extension
function pointers loaded at startup (the headers don't expose them on
old MinGW).

### 2.2 Sector-based level rendering

Levels are Wavefront OBJ. After load, triangles are sorted by material
ID and grouped into **sectors** (`objBuildSectors`). Each sector is one
batched draw call: bind diffuse + lightmap, draw all tris.

Per-material data lives in the `.mtl` file and supports custom flags
beyond standard Wavefront — `# lm_map`, `# tile_scale`, `# tile_offset`,
`# alpha_test`. See `level-design.md` for the authoring side.

Levels without a `mtllib` (single-texture mode) fall back to loading
`assets/levels/diffuse.png` + `assets/levels/lightmap.png`.

### 2.3 Tiling diffuse via box mapping

Diffuse UVs are computed at render time from world position
(`computeTilingUV`). The OBJ's own UV set is reserved for the
lightmap. Surfaces tile by world-meters (`# tile_scale`) without any
unwrapping. `GL_REPEAT` on diffuse, `GL_CLAMP_TO_EDGE` on the
lightmap.

The face's dominant normal axis picks the projection plane (XZ for
floors/ceilings, ZY for ±X walls, XY for ±Z walls) — Quake-style.

### 2.4 Alpha-test cutout materials

Tag a material with `# alpha_test [threshold]` in its MTL. The diffuse
PNG's alpha channel is preserved at load (RGBA), and the sector
renders with `GL_ALPHA_TEST` + `glAlphaFunc(GL_GREATER, threshold)`
(default 0.5). Used for chain-link, foliage, grates — anything where
a single texture has hard cutouts.

Translucent (alpha-blended) surfaces are **not** supported yet — they'd
require back-to-front sorting per frame. See
`docs/PLAN_BILLBOARDS_EMISSIVE.md` for that and emissive-map plans.

### 2.5 Dynamic flashlight (HL1-style lightmap)

Per-texel real-time lighting on a fixed-function GPU. At load, the
level mesh's UVs are rasterized into a `worldPosMap[y][x] → worldXYZ`
lookup table. Each frame:

1. Raycast from the camera along the look direction.
2. If hit, walk the affected texels in UV space and add a bright
   falloff into the lightmap RGB pixel buffer in CPU RAM.
3. Re-upload the modified region via `glTexSubImage2D`.

This gives per-texel resolution the GPU can't otherwise produce on
2-TMU hardware. When the flashlight turns off, the baked lightmap is
restored from a copy.

### 2.6 Player gun + entity rendering

`renderGun` draws a textured first-person mesh fixed to view space
with a muzzle-flash flicker. Entity meshes are either static OBJs
(decorations, doors) or animated **IQM** models with CPU skinning —
bone matrix palette is computed per frame, vertices transformed in
software (per the no-shaders constraint).

### 2.7 2D HUD canvas

`ui.h` provides a virtual coordinate system: 540 virtual units tall,
width scales with aspect ratio, origin centered, Y growing down.
`uiText` / `uiQuad` / `uiBar` / `uiIcon` draw into this canvas.
`uiBegin`/`uiEnd` switch GL into ortho mode for the duration.

Two text paths:

- Built-in 8x8 ASCII bitmap atlas (built at init from embedded
  font8x8 data, no disk asset).
- **BMFont (AngelCode) atlases** — `.fnt` text file + RGBA PNG atlas.
  `uiFontLoad` deduplicates by source path so multiple logical names
  can alias one atlas without double-uploading the texture.

The shipping HUD uses Orbitron in two sizes via BMFont.

---

## 3. Asset pipeline

### 3.1 PNG textures (only)

Loader is `stb_image` vendored at `vendor/stb/stb_image.h`. No BMP, no
TGA — the project migrated to PNG so alpha works reliably across
toolchains. The loader exposes a `keepAlpha` flag so cutout materials
get RGBA and tiled diffuse stays RGB. A `TexCache` keyed by
`(path, wrapMode, keepAlpha)` ensures the same PNG isn't uploaded
twice.

### 3.2 Wavefront OBJ + custom MTL

OBJ parsing handles vertices, texcoords, normals, faces (triangles
or auto-triangulated quads), `mtllib`, and `usemtl`. Per-material
slots:

| Field | Source | Purpose |
|---|---|---|
| `map_Kd` | standard MTL | Tiling diffuse PNG |
| `# lm_map` | custom | Per-sector lightmap PNG (defaults to `<materialname>_lm.png`) |
| `# tile_scale` | custom | Texture repeats per world meter |
| `# tile_offset` | custom | UV offset on the diffuse |
| `# alpha_test [threshold]` | custom | Enable cutout transparency |

### 3.3 IQM skeletal models

Inter-Quake Model with CPU skinning. Each frame: interpolate two
keyframes' bone transforms, blend, and transform vertex positions +
normals into a per-frame output buffer. The render loop streams those
into `glBegin(GL_TRIANGLES)`. IQMs bake a material filename into the
binary; the loader auto-rewrites that to `.png` so older models with
baked-in `.tga`/`.bmp` keep working.

### 3.4 WAV sounds

16-bit PCM, mono or stereo (stereo is downmixed to mono on load so
OpenAL can spatialize it). Other formats (8-bit, ADPCM, float) are
rejected with a stderr line.

### 3.5 Lua manifest (assets.lua)

A single Lua table declares everything game code references by short
logical name:

```lua
return {
    sounds = { fire = "...", steps = { "step1.wav", "step2.wav" } },
    models = { mrfixit = "assets/models/mrfixit.iqm", ... },
    textures = { wood1 = "...", office = "..." },
    fonts    = { default = "...", orbitron = "...", ... },
    levels   = { level1 = { obj = "...", ent = "..." } },
}
```

`scriptLoadAssets` walks the table at startup and registers every
entry into the right runtime structure (`SoundLibrary` /
`AssetRegistry` / `UiFontLib`). `.ent` files reference assets by
logical name (`tex=wood1`); unknown names fall through as raw paths.

---

## 4. Audio (OpenAL 1.1)

### 4.1 Variant sound groups

Every `SoundLibrary` entry holds up to 4 buffers under one name.
`name = "path"` in `assets.lua` makes a 1-variant entry; an array
makes a group. `sndLibPick(lib, name)` returns a uniformly-random
non-repeating variant per call (exclusion-shift algorithm,
`r = rand() % (n-1); if r >= last: r++`). A 1-variant group
short-circuits to the only buffer. Per-entry `lastIdx` so groups
don't interfere with each other.

Used today for footsteps; the same structure works for any "alt
sound" (gunshots, hits, voice barks).

### 4.2 Positional 3D audio

- `sndUpdateListener(snd, pos, forward, up)` — called once per frame
  from the main loop after camera math. Pushes
  `AL_POSITION` + `AL_ORIENTATION` to OpenAL.
- `sndPlay(snd, buf)` — head-relative (2D). Forces
  `AL_SOURCE_RELATIVE = TRUE` and position 0 per call so a previously
  positioned source can't leak coordinates into the next 2D play.
- `sndPlayAt(snd, buf, Vec3 pos)` — world-positioned (3D). Sets
  `AL_SOURCE_RELATIVE = FALSE`.

Default OpenAL inverse-distance attenuation (reference 1m,
rolloff 1) is left as-is. Mono sources spatialize automatically;
stereo would play head-relative by spec — but the WAV loader
downmixes to mono so positional always works.

### 4.3 Source pool

16 OpenAL sources allocated at init. `sndPickSource` returns the
first non-playing source, or steals source 0 if all are busy.
Fire-and-forget; callers don't track source IDs.

---

## 5. Physics (Bullet 3.25)

### 5.1 Level collider

Static `btBvhTriangleMeshShape` built from the level OBJ's triangles
at load. Concave geometry works (kneeholes in desks, arches, etc.).
Built **before** `objBuildSectors()` since sector-sorting reorders
triangle indices and would break the collider.

### 5.2 Player

`btKinematicCharacterController` (subclassed as
`FpsCharacterController` to expose `m_verticalVelocity` for ceiling
clamping). 1.75 m tall capsule, 0.225 m radius, step-up height 0.35 m,
max slope 50°, gravity ~24 m/s² (snappier than Earth gravity, classic
Quake/HL1 feel).

A `physStep` post-step raycast clamps the capsule down if its top
penetrated a ceiling (Bullet's char controller doesn't always handle
tight overhead clearance correctly).

### 5.3 Entity colliders

| Collide flag | Collider |
|---|---|
| `collide=box` | AABB from mesh verts → static `btBoxShape` |
| `collide=trimesh` (or `collide=2`) | exact `btBvhTriangleMeshShape` from the OBJ |
| (none) | no collider |

Doors get **kinematic** boxes — mass 0 but `CF_KINEMATIC_OBJECT` so
Bullet pushes the player out of the way when the door reposition.

### 5.4 Convex sweep "stop if blocked"

`physMoveKinematicBox` sweeps the door's shape from current to target
transform. If the sweep hits anything except the body itself, the
move returns 0 and the door's progress doesn't advance — caller
(door state machine) keeps state and retries next tick.

### 5.5 Raycasting

`physRaycast(world, from, dir, maxDist, *hitOut)` against the static
filter only (level + decoration colliders, not the player capsule).
`hitOut` may be NULL when only hit/no-hit matters (e.g. nav LOS).

---

## 6. Entities & gameplay

### 6.1 Authoring (`.ent` files)

Plain-text, one entity per line:

```
type name group posX posY posZ rotY [key=value ...]
```

`name`/`group` of `-` means none. Entity types currently parsed:

| Type | Purpose |
|---|---|
| `player` | Spawn point (transform-only) |
| `decoration` | Static prop with optional mesh + collider |
| `item` | Pickup-shaped decoration (health/ammo/key) |
| `enemy` | NPC slot (AI is a stub today; structure is in place) |
| `platform` | Vertical-moving kinematic |
| `switch` | Activator with a target name/group |
| `trigger` | AABB volume that fires its target on player overlap |
| `door` | Slide or rotate kinematic; auto-close timer |
| `waypoint` | Position-only nav node |

### 6.2 Triggers + activation chain

A `trigger` lists `target=<name-or-group>`. Walking inside its AABB
calls `entActivate(target)` once (or repeatedly if `once=0`). That
activation cascades — e.g. a switch can target a door's group.
`ent_activate(target)` is also exposed to Lua.

### 6.3 Doors

`motion=slide` (axis displacement) or `motion=rotate` (around an axis,
degrees in `amount`). `auto_close=<seconds>` reverses after holding
open. The convex-sweep test gates motion: a door blocked by the
player won't fight, it just pauses and resumes when clear.

---

## 7. Navigation

### 7.1 Waypoint graph

`navInit` harvests every `waypoint` entity into a flat `Vec3` array.
For each pair of nodes it runs an LOS raycast at eye height; if
clear, an edge is recorded in a packed bitset.

### 7.2 A* pathfinding

`navFindPath(g, phys, from, to, outPath, maxLen)` returns a sequence
of node indices. Used today only for a debug print on key `N`; AI
hookup is the next gameplay milestone.

---

## 8. Scripting (Lua 5.1)

### 8.1 Hooks

`scripts/main.lua` runs once at startup, then `on_start()` fires after
the level + entities are loaded. Define any subset; missing hooks are
no-ops.

### 8.2 Engine bindings

| Lua | Effect |
|---|---|
| `ui_show_message(text [, seconds])` | Transient centered HUD message |
| `snd_play(name)` | Head-relative play of a registered buffer/group |
| `snd_play(name, x, y, z)` | World-positioned play |
| `ent_activate(target)` | Trigger by entity name or group |
| `print(...)` | Routed through `conLogf` so `print` shows in the in-game console |

### 8.3 Console eval

The dev console (`` ` `` to toggle) `luaL_loadstring` + `lua_pcall`s
each entered command with `scr_traceback` as the error handler.
Result or error is printed back through `conLogf`. The full Lua
state — global tables, registered bindings — is available, so the
console doubles as a REPL.

---

## 9. UI shell (menu + console + HUD)

### 9.1 Top-level state machine

`AppState` has `mode = MODE_MENU | MODE_GAME` and a `Game*` that's
NULL until first New Game. Mode transitions toggle mouse grab — off
in menu (cursor roams the UI), on in-game (mouselook).

### 9.2 Screens

A small fixed `ScreenStack` holds tagged-union screens
(`MainMenu` / `Options` / `Dialog`). Screens render bottom-up so
overlays dim what's underneath with a 50% black quad. Actions that
need game side effects (New Game / Continue / Exit) go through
`AppState::pendingAction` rather than touching the running game from
inside menu code.

The "New Game with a session already running" path pushes a
confirm dialog; OK and the first-time path share one
teardown/reinit branch.

### 9.3 Loading splash

`drawLoadingScreen` paints a cover-fit `loading_bg` background and a
"LOADING" label, then swaps buffers once. Called from the
`PENDING_NEW_GAME` branch right before `gameInit` blocks the thread,
so the window shows something during the synchronous level + physics
load.

### 9.4 In-game dev console

Quake-style drop-down. Toggled with `` ` ``. Writes go to stdout AND
into a fixed scrollback ring through `conLogf` — that's also the
engine's general logging function. Lua's `print` is rebound via
`scriptInstallConsolePrint` so script output lands here too.
Mouselook + fire are paused while the console is open so typing
doesn't double as gameplay.

### 9.5 HUD

Crosshair, FPS counter, HUD message strip from `ui_show_message`,
flashlight battery indicator, gun render. Backtick (`` ` ``) for
console, `B` for collider wireframes, `N` for nav graph debug overlay,
`P` to print player position to stdout.

---

## 10. Platform notes

### 10.1 Refresh-rate preservation (Win)

SDL 1.2's fullscreen path calls `ChangeDisplaySettings` without a
refresh frequency, so the monitor drops to 60 Hz. `main.cpp` samples
the configured desktop rate via `EnumDisplaySettings(ENUM_REGISTRY_SETTINGS)`
before `SDL_Init` and re-applies it with `ChangeDisplaySettingsEx`
after `SDL_SetVideoMode`. No-op on Linux.

### 10.2 OpenAL DLL split

Win10 ships with `OpenAL32.dll` (1.25.1). Win98 needs the older
1.9.563 build that's vendored as `OpenAL32-win98.dll` — rename to
`OpenAL32.dll` for that target. See `docs/plan-openal.md` for the
migration history.

### 10.3 Object cache

`build.bat` and `build_win10.bat` keep Bullet's three unity-build
objects + Lua's unity object in `raw/obj/` and skip recompiling
them when present (~60-90 s saved per build). They do **not** track
header changes — bumping Bullet or switching toolchain means deleting
`raw/obj/{bl,bc,bd,lua}.o` to force a rebuild.

### 10.4 Line-ending pin

`.gitattributes` pins `*.bat` / `*.cmd` to `eol=crlf` because
`core.autocrlf=input` doesn't convert LF→CRLF on checkout, and cmd.exe
chokes on LF batch files with `'ocal' is not recognized`-style errors.

---

## 11. Controls (default)

### In-game

| Key | Action |
|---|---|
| WASD | Move |
| Mouse | Look |
| Space | Jump |
| Left-click | Fire |
| F | Toggle flashlight |
| `` ` `` | Toggle console |
| Esc | Back to menu (game state preserved) |
| B | Collider wireframe overlay |
| N | Nav graph overlay + A* path test print |
| P | Print player pose to stdout |

### Menu

| Key | Action |
|---|---|
| Up/Down | Cycle items |
| Enter | Activate |
| Esc | Back / cancel dialog |
| Left/Right | Switch dialog buttons |
| Mouse | Hover + click |

---

## 12. Limitations / not-yet

These are explicitly out of scope or planned but unimplemented:

- **No alpha-blended translucency.** Cutouts only. Glass/water needs
  back-to-front sorting work — see `docs/PLAN_BILLBOARDS_EMISSIVE.md`.
- **No light-effect billboards or emissive maps yet.** Plan
  documented; both fit on the MX 440 GPU floor.
- **No enemy AI behavior.** Entity slot exists, nav graph + A* are
  ready, the wiring isn't done.
- **No save/load.** "Continue" preserves the in-process game state
  across the menu but a fresh launch starts over.
- **No music streaming.** OpenAL plays one-shot buffers; ogg-vorbis
  streaming would be a separate addition.
- **No multiplayer, ever.** Not a goal of the project.
