# Adding a mini-game inside SOOB-Engine

A thought-experiment design doc: what it would take to push something
like Find5 (the 2D spot-the-difference game) on top of the 3D FPS as a
mini-game — for example, the player walks up to a CRT in the office,
presses E, and a 2D puzzle takes over the screen until they finish or
back out.

This isn't a roadmap or a commitment, just the shape of the work. The
scene-stack extraction we did for the Lua menu was deliberately
designed to enable this, so most of the architecture is already paid
for; the rest is a few C-side glue points and a Find5-side refactor.

## What already works

- **Engine surface is identical between the two projects.** Both
  consume SOOB-Core. Find5's bindings (`draw_region`, `draw_text`,
  `draw_ellipse`, `draw_quad`, `draw_bg`, `draw_blur`, `view_size`,
  `opt_get/set/save/load`, `mouse_pos`, `key_down`, sound + music)
  are also available inside SOOB-Engine. No engine work needed on this
  axis.

- **Scene stack is the host mechanism.** A mini-game is just a scene
  pushed on top of whatever's running. Popping it returns control to
  the scene below. `engine.scene` (in SOOB-Core) handles enter / exit
  lifecycle, transparent overlays, and dispatcher wiring.

- **Per-game asset manifest is just a Lua table.** `assets.lua` can
  grow new entries for mini-game textures / regions / sounds, or
  alternatively the mini-game can load its own bundle lazily — both
  approaches are tractable.

## What needs extending in SOOB-Engine

### 1. MODE_GAME has to dispatch to Lua scenes too

Today `main.cpp`'s `MODE_GAME` branch runs the 3D game loop directly
and never calls `scriptCall*` — that's by design, since menu work
shouldn't tick the 3D world. To let a mini-game scene run on top of
the 3D game we need:

```c
if (app.mode == MODE_GAME) {
    if (scene_is_active(&script)) {
        /* Mini-game owns the frame. */
        while (SDL_PollEvent(&event)) {
            /* route input to scriptCallKeyDown / MouseDown / etc. */
        }
        scriptCallUpdate(&script, dt);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        uiBegin(&ui);
        scriptCallRender(&script);
        uiDrawMessage(&ui);
        uiEnd(&ui);
    } else {
        /* existing 3D game loop unchanged */
    }
}
```

`scene_is_active` would be a tiny Lua call (e.g. a binding that
returns `scene.depth() > 0`).

The trigger to push the mini-game comes from somewhere game-side: an
entity interaction (E on a `computer` entity), a console command, a
level-script event, etc. Cleanest: an entity activation routes into a
Lua hook that calls `scene.push(minigame.find5())`. The existing
`ent_activate` binding (in `script_ext.h`) plus a small Lua-side
dispatcher would do it.

### 2. Asset namespacing / lazy loading

Find5 declares regions like `image_1a`, `image_1b`, `star`,
`pause_button_up`. SOOB-Engine's `assets.lua` doesn't have those today.
Three options:

- **Inline merge.** Add Find5's textures / regions to SOOB-Engine's
  `assets.lua` (optionally under a `find5_` prefix to avoid name
  collisions later). Simple, but everything loads at boot — fine for
  one mini-game, gets heavy if there are several.

- **Lazy asset bundle.** Expose a `assets_load_bundle(path)` Lua
  binding that walks a *second* manifest file at runtime. The engine
  already has `scriptLoadAssets` — a Lua-callable variant is a small
  binding. The mini-game scene's `enter()` loads its bundle; assets
  stay in the cache after `exit()` (the cache is bounded). Probably
  what you'd want long-term with multiple mini-games.

- **Per-mini-game folder.** `assets/minigames/find5/` with its own
  `assets.lua` next to the textures. The launcher loads it via the
  bundle mechanism above. Same idea, just a directory convention.

### 3. Mouse grab + simulation pause

The 3D mode grabs the cursor for mouselook. A mini-game like Find5
needs the cursor free for clicks. So the transition is:

- **Enter mini-game:** `SDL_WM_GrabInput(SDL_GRAB_OFF)` +
  `SDL_ShowCursor(SDL_ENABLE)`. Pause physics + entity updates (skip
  `physStep` / `entUpdate` / `updatePlatforms` / `updateDoors` while a
  scene is active).

- **Exit mini-game:** re-grab, re-hide cursor, resume simulation.

That's a small C-side branch keyed on `scene_is_active`. Player state
(position, velocity, health, etc.) is preserved because none of the
game updates ran while the scene was up.

## What needs adjusting on the Find5 side

### Wrap main.lua as a scene constructor

Find5's `scripts/main.lua` today defines globals (`on_update`,
`on_render`, `on_keydown`, etc.) at file scope and uses a module-level
state machine. To embed:

```lua
-- scripts/minigames/find5.lua  (inside SOOB-Engine)
local M = {}

function M.new()
    local scene = {}
    -- ... all the existing main.lua state, scoped to `scene` ...
    function scene:update(dt)    -- existing on_update body ...
    function scene:render()       -- existing on_render body ...
    function scene:keydown(name)  -- existing on_keydown body ...
    function scene:mousedown(x, y, btn) ... end
    function scene:enter() ... end
    function scene:exit()
        -- save scores, restore mouse grab via a binding, etc.
    end
    return scene
end

return M
```

Then in the launcher: `scene.push(require("minigames.find5").new())`.

Most of the body is already factored — Find5's `main.lua` already has
discrete handler functions, just at file scope.

### Canvas size

Find5 designs for a 480-tall canvas; SOOB-Engine runs 540. Two paths:

- **Accept slight layout drift.** Find5's UI already calls
  `view_size()` per frame for anchoring, so headers and footers
  re-anchor naturally. Card-style layouts (portraits) might look a
  little roomier vertically.
- **Pass a target canvas.** A bit more work; would need the engine
  bindings to honor a sub-canvas override, which they currently don't.
  Probably not worth it for a one-off.

### Persistence

Find5 writes to `find5.dat` via `opt_save`. Inside SOOB-Engine the engine's
opt file is `soob.dat` (set in `scriptInit`'s `optFile` param).
Find5's `opt_set("hs_portraits", …)` will write into SOOB-Engine's opt
store — no file conflict, just key namespacing. To keep things tidy,
either:

- Prefix Find5's option keys when embedded (`find5_hs_portraits`).
- Or expose a `opt_set_in(file, key, value)` variant that targets a
  separate file. Bigger surface; probably overkill.

## Reusable parts that could move to the engine

- **Dialog system.** Find5's `scripts/dialog.lua` (modal drop-in /
  shoot-out animations, scene-style update/render/click handling) is
  generic. Could live in SOOB-Core under `engine/dialog.lua`. Both
  the menu (`confirm_dialog` could re-use it) and any future
  mini-game would share one dialog implementation.
- **Score popup / animated counter.** Same story — generic 2D effects.
- **Timer bar.** Same.

These would be small extractions, each independent of the mini-game
work. Doing them first would make the Find5 embed even smaller.

## TL;DR sequence if you ever pursued this

1. Extract `engine/dialog.lua` from Find5 into SOOB-Core. Update
   Find5's `dialog.lua` to be a thin re-export so existing call sites
   keep working.
2. Add a `scene_is_active()` binding (or just `scene_depth()`) in
   SOOB-Core. Trivial.
3. Add MODE_GAME-scene dispatch in `main.cpp`: when a scene is on top
   while in game mode, the scene owns input + render and the 3D
   simulation skips.
4. Add mouse-grab + simulation-pause around the dispatch.
5. Decide the asset-loading flavor (inline vs lazy bundle vs per-
   minigame folder) and implement.
6. Refactor Find5's `scripts/main.lua` into `find5.new()` returning a
   scene table.
7. Hook a launcher — could be an entity interaction (`computer` ent
   with an `activate=push_find5` field) wired through `ent_activate`,
   or a console command for testing.
8. Decide the persistence story (prefix keys vs separate file).

The scene-stack work already paid for most of the design. The
remaining items are mostly small C-side glue plus moving Find5's
existing code into a scene factory.
