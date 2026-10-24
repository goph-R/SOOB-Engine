# Level Design Guide (Blender)

How to create levels with diffuse textures and baked lightmaps for the SOOB-Engine FPS engine.

## Prerequisites

- Blender 2.8+ (any modern version)
- The level is exported as Wavefront OBJ with UVs
- Textures are exported as **PNG** (24-bit RGB, or 32-bit RGBA for alpha-test cutouts). The engine no longer reads BMP or TGA — see `docs/features.md` §3.1 for the asset pipeline summary.

## Step 1: Model the Level

1. Open Blender, delete the default cube
2. Build your level geometry using basic meshes (cubes, planes)
   - Use `Shift+A` > Mesh > Cube/Plane to add geometry
   - Scale and position to form rooms, corridors, ramps
   - Keep geometry simple - this targets a Pentium 4
3. Join all level pieces into one object (`Ctrl+J`)
4. Make sure normals point inward (into rooms):
   - Enter Edit Mode (`Tab`)
   - Select All (`A`)
   - `Mesh` > `Normals` > `Recalculate Outside`, then `Flip` if needed
   - You can display normals via Overlays > Face Orientation (blue = front, red = back)

## Step 2: Materials and Tiling Textures

### Assigning Materials (Sectors)

Each material in Blender becomes a **sector** in the engine. Faces sharing the same material
are batched together and share a diffuse texture + lightmap.

1. Create materials for different surface types (e.g., `brick_wall`, `concrete_floor`, `metal_door`)
2. In Edit Mode, select faces and assign them to materials via the Material Properties panel

### Setting Up Tiling Diffuse Textures

The engine computes diffuse texture coordinates from world position (box mapping), so
textures tile automatically without UV unwrapping. To preview this in Blender:

1. In the **Shader Editor** for each material, set up this node chain:
   ```
   [Texture Coordinate] --Object--> [Mapping] --Vector--> [Image Texture] --Color--> [Principled BSDF]
   ```
2. On the **Mapping** node:
   - **Scale** controls tiling density (1.0 = one repeat per meter, same as `tile_scale 1.0`)
   - **Location** controls texture offset (same as `tile_offset` in MTL)
3. On the **Image Texture** node, load your tiling texture (brick, concrete, etc.)
4. Switch viewport to Material Preview to see the tiling

### Single-Material Levels (Simple Mode)

If you don't use materials (no `mtllib`/`usemtl` in the OBJ), the engine falls back to
loading `assets/levels/diffuse.png` and `assets/levels/lightmap.png`. This is the original
behavior — one texture atlas for the whole level.

## Step 3: UV Unwrap for Lightmap

The OBJ's UV coordinates are used **only for the lightmap**, not for diffuse textures.

1. Select your level object, enter Edit Mode
2. Select All (`A`)
3. `UV` > `Smart UV Project`
   - Angle Limit: 66 degrees works well
   - Island Margin: **0.04** for levels that use the dynamic flashlight; 0.02–0.03 is fine if you don't. The flashlight rasterizes triangles into UV space and writes near triangle edges every frame, so a tighter margin produces visible spill artifacts. The widely-quoted "tight as possible" advice does not apply here.
4. Open UV Editor to verify the unwrap looks reasonable

## Step 4: Set Up Lighting for Lightmap

1. Switch to the Rendered viewport or Layout workspace
2. Add lights to your scene:
   - `Shift+A` > Light > Point/Spot/Area
   - Position lights in rooms, corridors, near doorways
   - Adjust intensity and color for mood
3. Optional: Add an HDRI or ambient light for base illumination
4. Use Cycles render engine for best quality bakes:
   - Properties > Render > Render Engine: Cycles

## Step 5: Bake Textures

### Important: All materials need bake setup

For every material on your level object, you must add an Image Texture node in the Shader
Editor that is **selected (highlighted)** but **NOT connected** to anything. This tells
Blender where to bake to.

### Bake Diffuse (Single-Material Mode)

1. Select your level object
2. Create a new Image in the UV Editor (e.g., `diffuse` 1024x1024)
3. In each material's shader node tree, add an Image Texture node
   - Select the `diffuse` image
   - **Make sure this node is selected (highlighted)** but NOT connected
4. Properties > Render > Bake:
   - Bake Type: `Diffuse`
   - Under Influence, check only `Color` (uncheck Direct, Indirect)
   - Click `Bake`
5. Save the image: Image Editor > Image > Save As > **PNG format**
   - Save as `diffuse.png` in `assets/levels/`

### Bake Lightmap (Single-Material Mode)

1. Create another new Image in UV Editor (e.g., `lightmap` 512x512 or 1024x1024)
2. In each material's shader node tree, select the Image Texture node
   - Change it to the `lightmap` image
   - Again, selected but NOT connected
3. Properties > Render > Bake:
   - Bake Type: `Diffuse`
   - Under Influence, check `Direct` and `Indirect` (uncheck `Color`)
   - This bakes only the lighting, not the surface color
   - Click `Bake`
4. Save as `lightmap.png` in `assets/levels/`

### Bake Per-Sector Lightmaps (Multi-Material Mode)

When using multiple materials, bake a separate lightmap for each material/sector:

1. For each material, create a lightmap image (e.g., `brick_wall_lm` at 256x256 or 512x512)
2. In each material's shader nodes, add an Image Texture node with that material's lightmap image selected
3. Bake with Diffuse > Direct + Indirect (no Color) as above
4. Save each as `<materialname>_lm.png` (this matches the engine's default naming convention; override with `# lm_map` in the MTL if you put the file somewhere else)

## Step 6: Export OBJ

### Single-Material Export

1. Select your level object
2. `File` > `Export` > `Wavefront (.obj)`
3. Export settings:
   - Check: `Selection Only`
   - Check: `Write Normals`
   - Check: `Include UVs`
   - Forward: `-Z Forward`
   - Up: `Y Up`
   - Uncheck: `Write Materials` (we handle textures ourselves)
4. Save as `test_level.obj` (or your level name) in the SOOB-Engine folder

### Multi-Material Export

1. Select your level object
2. `File` > `Export` > `Wavefront (.obj)`
3. Export settings:
   - Check: `Selection Only`
   - Check: `Write Normals`
   - Check: `Include UVs`
   - **Check: `Write Materials`** (enables mtllib/usemtl in the OBJ)
   - Forward: `-Z Forward`
   - Up: `Y Up`
4. Save as `test_level.obj` in the SOOB-Engine folder
5. Edit the generated `.mtl` file to add engine-specific properties:

```
newmtl brick_wall
map_Kd brick.png
# lm_map brick_wall_lm.png
# tile_scale 0.5
# tile_offset 0.0 0.0

newmtl concrete_floor
map_Kd concrete.png
# lm_map concrete_floor_lm.png
# tile_scale 1.0
# tile_offset 0.25 0.1

newmtl chainlink_fence
map_Kd chainlink.png
# lm_map chainlink_lm.png
# alpha_test 0.5
```

### MTL Custom Properties

| Property | Format | Default | Description |
|---|---|---|---|
| `map_Kd` | `map_Kd filename.png` | (none) | Tiling diffuse texture (`GL_REPEAT`) |
| `# lm_map` | `# lm_map filename.png` | `<name>_lm.png` | Per-sector lightmap (`GL_CLAMP_TO_EDGE`) |
| `# tile_scale` | `# tile_scale 0.5` | `1.0` | Texture repeats per world unit |
| `# tile_offset` | `# tile_offset 0.25 0.1` | `0.0 0.0` | UV offset (matches Blender Mapping > Location) |
| `# alpha_test` | `# alpha_test [threshold]` | off (`0.5` if enabled) | Cutout transparency. The diffuse PNG must be 32-bit RGBA. Pixels whose alpha is ≤ threshold are discarded. Used for fences, foliage, grates. |

## Step 7: Run the Game

### Single-Material Mode

Place these under `assets/levels/`:
- `test_level.obj` — the level geometry with UVs
- `diffuse.png` — the wall/floor texture atlas
- `lightmap.png` — the baked lighting

### Multi-Material Mode

Place these under `assets/levels/`:
- `test_level.obj` — geometry with `mtllib` reference
- `test_level.mtl` — material definitions
- Tiling diffuse textures (e.g., `brick.png`, `concrete.png`) — typically under `assets/textures/`
- Per-sector lightmaps (e.g., `brick_wall_lm.png`, `concrete_floor_lm.png`)

The engine automatically detects whether materials are present and switches between
single-texture and sector-based rendering.

### Player spawn

The player spawn position is **not** hardcoded — it comes from a `player`
entity in `test_level.ent`:

```
player spawn - 0 1 0 0
```

(`type name group posX posY posZ rotY`). If no `player` entity is present,
the engine falls back to (0, 2, 0). Add other entities (decorations, doors,
triggers, waypoints) to the same `.ent` file. See `docs/features.md` §6 for
the entity types and `.ent` syntax.

## Tips

- **Texture size**: 512x512 is fine for a P4, 1024x1024 is the practical max
- **Keep geometry low-poly**: Aim for under 5000 triangles per level
- **Slopes**: Any angle up to 50 degrees is walkable by the player
- **Player spawn**: defined by a `player` entity in the level's `.ent` file (see "Player spawn" above). Make sure there's floor under it.
- **Scale**: 1 unit = 1 meter. Player is 1.75 m tall. Standard room height is ~3–4 m.
- **Test often**: Export and run the game to check scale and feel
- **Lightmap resolution**: Lower res (256×256) gives a softer, more retro look
- **PNG format**: 8-bit per channel. Use 32-bit RGBA only when you actually need alpha (cutouts, fonts) — the loader auto-flattens "all alpha = 255" inputs to RGB to avoid wasting VRAM.
- **UV island margin**: 0.04 if the level uses the dynamic flashlight; 0.02–0.03 otherwise.
- **VRAM budget (32MB GPU)**: ~24 MB for textures. Small tiling textures (128×128) + per-sector lightmaps (256×256) fit easily for 10+ rooms.

## File Format Notes

The engine reads standard Wavefront OBJ with these features:
- Vertices (`v x y z`)
- Texture coordinates (`vt u v`)
- Normals (`vn x y z`)
- Faces (`f v/vt/vn v/vt/vn v/vt/vn`) - triangles and quads (auto-triangulated)
- Material library (`mtllib filename.mtl`) - optional, enables multi-material mode
- Material switch (`usemtl name`) - assigns faces to materials/sectors

Textures must be PNG. The loader is `stb_image` (vendored at
`vendor/stb/stb_image.h`) — anything `stbi_load` accepts works, but the
engine only opens files whose name ends in `.png`. BMP and TGA are no
longer supported; the migration history lives in commit `456693f`.
