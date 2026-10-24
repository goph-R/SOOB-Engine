# Verifying the SOOB Engine Blender Plugin

A shakedown checklist for `tools/soob_export.py`. Each item is
independent — if one fails, stop and fix before moving on. "Reload"
means **F3 → Reload Scripts** in the symlink + reload workflow (see
README of the addon install for setup).

The plugin's design lives in `docs/PLAN_BLENDER.md`; this file is the
acceptance test for the v1 implementation.

## Install & registration

- [ ] Install via **Preferences > Add-ons > Install...**, enable the
      addon. No errors in the Blender system console.
- [ ] After **Reload Scripts**, the addon stays enabled (no ghost
      panels, no "duplicate class" errors).
- [ ] Disable, re-enable: `register` / `unregister` are symmetric —
      no leaked properties (`Object.soob`, `Scene.soob_show_overlays`)
      and no leaked draw handlers (close + reopen the 3D View;
      overlays still work).

## Object Properties panel (Steps 2 + 5)

- [ ] Selecting any object shows the **SOOB Entity** section in Object
      Properties.
- [ ] **Type** dropdown lists all 11 values; default is *None (Level
      Geometry)*.
- [ ] Selecting *None* shows the "Exported as level geometry" hint.
- [ ] Each non-*None* type reveals the right subset of fields:
  - [ ] **Player** — message only, no extra fields.
  - [ ] **Decoration** — common + visual binding (mesh/tex/iqm/anim).
  - [ ] **Item** — common + visual + Item Type enum.
  - [ ] **Enemy** — common + visual + health/speed/sight.
  - [ ] **Platform** — common + visual + path/move/speed/enabled/face_path.
  - [ ] **Switch** — common + visual + target.
  - [ ] **Trigger** — common + target/once + scale-hint label.
  - [ ] **Door** — common + visual + motion/axis/amount/speed/auto_close.
  - [ ] **Waypoint** — message only, no extra fields.
  - [ ] **Path Node** — common + order.
- [ ] Switching type back and forth preserves type-specific values
      (PropertyGroups stay attached).
- [ ] Values persist across **Save + Close + Reopen** of the .blend.

## OBJ + MTL writer (Step 3)

- [ ] Active mesh with no materials → single-material OBJ: no
      `mtllib` emitted, no `.mtl` written, file size within ~10% of
      Blender's built-in exporter for the same mesh.
- [ ] Active mesh with 2 materials → MTL with 2 `newmtl` blocks; OBJ
      has `mtllib` + 2 `usemtl` switches in material-slot order.
- [ ] Material with a Mapping node (Scale.x = 0.5, Location = (1, 2))
      emits `# tile_scale 0.5000` + `# tile_offset 1.0000 2.0000`.
- [ ] Material with `soob_tile_scale` custom property = 0.25 overrides
      the Mapping node value.
- [ ] Material with no Image Texture in its shader → `map_Kd` omitted
      (no spurious empty line).
- [ ] `vn` count after export is significantly smaller than the loop
      count on axis-aligned meshes (dedup working).
- [ ] Engine loads the exported OBJ: drop into `assets/levels/`, run
      `SOOB_w10.exe`, level renders correctly.
- [ ] Quads stay quads (no pre-triangulation); engine fan-triangulates
      on load without complaints.
- [ ] Object with negative scale exports correctly (no inverted
      normals breaking lightmap; axis swap handles sign).

## Lightmap bake (Step 4)

- [ ] First export with bake on → `<material>_lm.png` files appear
      next to the OBJ. Image dimensions match the chosen resolution.
- [ ] Each PNG visually shows baked lighting (no surface color, just
      shadows and gradients).
- [ ] Re-export → same files overwritten (bake idempotent, no stray
      `<material>_lm.001.png` duplicates).
- [ ] `_SOOB_LM` Image Texture node appears in each material's shader
      tree, disconnected, parked off to the side.
- [ ] Changing **Lightmap Resolution** → next bake produces images at
      the new size.
- [ ] Mesh with no `Lightmap` UV map + Auto-create UV unchecked →
      bake fails fast with a clear error toast, no OBJ written.
- [ ] Mesh with no `Lightmap` UV map + Auto-create UV checked →
      Smart UV Project runs (margin 0.04), bake succeeds.
- [ ] Render engine + sample count restored to pre-bake values after
      the export completes (check Render Properties).
- [ ] Bake checkbox off → near-instant export, no PNGs created.
- [ ] Default cube scene with no lights → bake produces black images
      (expected; not an error).

## `.ent` writer (Step 6)

- [ ] Empty scene (no SOOB entities) → `.ent` file written with
      header comments + zero entity lines.
- [ ] Default field values omitted (e.g. `enabled=1` is the default
      for `platform`, should NOT appear in the line).
- [ ] Non-default fields emitted (e.g. `enabled=0`, `move=ping_pong`,
      `face_path=1`).
- [ ] `name` blank → object name used; `name_override="foo"` → `foo`
      used.
- [ ] `group` blank → `-` written; non-blank → emitted as-is.
- [ ] Position/rotation match Blender world transform with axis swap
      (engine `Y = blender Z`, `Z = -blender Y`).
- [ ] Trigger with Blender scale (1.5, 2.0, 1.5) → `size=1.5,2,1.5`
      (**half-extents**, NOT doubled — see `entity.h:465`).
- [ ] Path nodes emitted before platforms in output ordering.
- [ ] Engine's `entLoadFile` accepts the file without "unknown type"
      or parse errors (console log clean).
- [ ] Round-trip: export the current `test_level.ent`'s entities,
      diff — should be content-equivalent (ordering may differ).

## Viewport overlays (Step 7)

- [ ] Path-node objects in a shared group → cyan polyline connects
      them in `order` 0 → 1 → 2 order.
- [ ] Platform with `path=<group>` → magenta arrow from platform
      origin to the first path node of that group.
- [ ] Multiple platforms referencing the same group → only the
      first-seen platform gets the arrow (matches engine leader rules).
- [ ] Trigger entities → yellow wireframe boxes; box size matches
      `obj.scale × 2` visually (a Blender scale of 1.0 = 2 m cube).
- [ ] Each path_node has a magenta octahedron marker at its origin.
- [ ] Toggling **Show Entity Overlays** (N-panel) hides all SOOB
      gizmos; toggling back restores them.
- [ ] Overlay state per-scene (Scene property, not addon-wide), so
      opening a non-SOOB blend doesn't flash gizmos.
- [ ] Reload Scripts doesn't double-stack draw handlers (the line
      width stays at 2.0, not 4.0).
- [ ] Closing all 3D viewports + reopening still draws overlays
      (handler isn't tied to a specific viewport instance).

## Asset copy (Step 8)

- [ ] Checkbox off → no asset files touched (default behavior).
- [ ] Checkbox on, exporting into the repo root → no-op since
      destinations already exist; console shows "destination exists,
      skipped" lines.
- [ ] Checkbox on, exporting into a clean sandbox dir → referenced
      models / textures appear in `<sandbox>/assets/models/`,
      `<sandbox>/assets/textures/`.
- [ ] Logical names from `assets.lua` resolve correctly (`mesh=office_desk`
      → copies `assets/models/office-desk.obj`).
- [ ] Raw paths fall through (`tex=assets/textures/custom.png` →
      copies that exact file).
- [ ] Missing source file → console warning, export still completes.
- [ ] Existing destination → console "skipped" message, never clobbered.
- [ ] `assets.lua` absent (export far from repo) → console "skipped"
      message, no crash.

## Round-trip (Step 9)

The acceptance test for the addon as a whole: export
`assets/levels/test_level.*` to a sandbox directory using the addon,
point the engine at the sandbox, and verify the level looks and
behaves identically to the in-tree run.

### Procedure

1. Make a sandbox dir, e.g. `C:\tmp\soob_rt\assets\levels\`.
2. Open `raw/test-level.blend` in Blender (or wherever your master
   level source lives).
3. **Author entities** to match `assets/levels/test_level.ent`:
   - Add an Empty for each entity in the current `.ent`; set
     `soob.type` and the type-specific fields.
   - For `path_node` entities, give them the same `group` name
     (e.g. `lift1_path`) and set `order` 0/1/2.
   - For the lift platform, set `soob.platform.path = "lift1_path"`,
     `move = ping_pong`, `face_path = ✓`, `enabled = ✗`.
4. **Author the trigger** as a scaled Empty (or any object). Half-
   extent = object scale (so the engine sees the same volume the
   SOOB overlay draws).
5. `File > Export > SOOB Level`, point at
   `C:\tmp\soob_rt\assets\levels\test_level.obj`, leave defaults
   (bake on, copy assets on).
6. Copy SOOB's `OpenAL32.dll` + `SDL.dll` + the built
   `SOOB_w10.exe` into `C:\tmp\soob_rt\`, and copy `scripts/`,
   `assets.lua`, fonts, sounds, and music into matching
   subdirectories.
7. Run `SOOB_w10.exe` from `C:\tmp\soob_rt\`.

### Pass criteria

- [ ] Export the in-tree `test_level.blend` to a sandbox; engine
      boots cleanly.
- [ ] Visual parity: walls/floors/ceilings at the same positions,
      lighting close enough that you'd have to A/B compare for
      differences (samples=64 default vs. whatever the in-tree
      bake used).
- [ ] Decorations appear at the right positions and orientations.
- [ ] The lift platform's trigger fires on entry; the platform
      ping-pongs along its L-path with face-path rotation.
- [ ] Door entities open on activation, auto-close after 3 s.
- [ ] No console errors from `entLoadFile`, `pathTableBuild`,
      `physLoadLevel`, or `navInit`.
- [ ] Performance: in-engine FPS within 5% of the in-tree run
      (no surprise polygon explosion from accidentally-triangulated
      meshes).

## Cross-cutting / regression

- [ ] Toggling all dialog options on then off in sequence → no
      stale state between exports.
- [ ] Export operator's poll method blocks non-mesh active objects
      (selecting a Camera disables the menu entry).
- [ ] No `print` spam in normal operation (only the start-of-export
      banner, the bake log lines, and the final entity-count
      summary).
- [ ] No leaked images in `bpy.data.images` after repeated bakes
      (lightmap images are reused, not re-created).
- [ ] `Object.soob` accessible from the Python console
      (`bpy.context.object.soob.platform.path`) for tooling /
      scripting use.
