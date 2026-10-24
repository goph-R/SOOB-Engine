# Sector-Based Rendering + Tiling Textures + Blender Export Plugin

## Context

The engine currently uses one global `diffuse.bmp` + one global `lightmap.bmp` for the entire level. This doesn't scale beyond a couple of rooms on a 32MB GPU. The goal is to support bigger levels by using small tiling diffuse textures per material and per-sector lightmaps, plus a Blender plugin to automate the export.

## Key Design Decision: Two UV Problem

Tiling diffuse textures need repeating UVs (e.g., U=0..4 for a 4m wall). Lightmaps need atlas UVs (0-1 per sector). OBJ only has one `vt` set.

**Solution: Compute diffuse tiling UVs from vertex world position at render time** (Quake-style box mapping). The OBJ's `vt` coordinates are used exclusively for lightmap UVs. No second UV channel needed, no format changes.

## Phase 1: Engine Changes

### 1A. Data Structures (obj_loader.h)

Extend existing structs -- add after current definitions:

```c
struct Material {
    char name[64];
    char diffusePath[128];    /* map_Kd from MTL */
    char lightmapPath[128];   /* convention: <name>_lm.bmp, or custom # lm_map */
    float tilingScale;        /* world units per texture repeat, default 1.0 */
};

struct Sector {
    int materialId;
    int triStart;             /* first triangle index (after sorting) */
    int triCount;
};
```

Add `int materialId` field to existing `Triangle` struct.

Add to `ObjMesh`: `Material materials[32]`, `int numMaterials`, `Sector sectors[32]`, `int numSectors`.

### 1B. MTL Parser + usemtl Support (obj_loader.h)

- New `parseMtlFile()` function -- handles `newmtl`, `map_Kd`, custom `# lm_map`, `# tile_scale`
- In `objLoad()`: parse `mtllib` (calls parseMtlFile), `usemtl` (sets currentMaterialId)
- Each triangle gets `materialId = currentMaterialId`
- New `objBuildSectors()` -- sorts tris by materialId, creates Sector entries at boundaries

**Ordering constraint:** `physLoadLevel()` must be called BEFORE `objBuildSectors()` since sorting tris would break physics triangle indices.

### 1C. Texture Cache (texture.h)

```c
struct TexCache {
    struct { char path[128]; GLuint texID; int wrap; } entries[64];
    int count;
};
```

- `texCacheGet(cache, path, wrapMode)` -- returns cached GL ID or loads + caches
- Refactor `loadBMP`/`loadTGA` to accept wrap mode parameter (currently hardcoded to GL_CLAMP_TO_EDGE)
- Same file with different wrap mode = separate cache entries (correct behavior)

### 1D. Tiling UV Computation (main.cpp)

Box mapping from vertex world position + face normal:

```c
static void computeTilingUV(Vec3 *pos, Vec3 *normal, float scale, float *u, float *v)
{
    float ax = fabsf(normal->x), ay = fabsf(normal->y), az = fabsf(normal->z);
    if (ay >= ax && ay >= az) {        /* floor/ceiling -> XZ */
        *u = pos->x * scale;  *v = pos->z * scale;
    } else if (ax >= az) {             /* left/right wall -> ZY */
        *u = pos->z * scale;  *v = pos->y * scale;
    } else {                           /* front/back wall -> XY */
        *u = pos->x * scale;  *v = pos->y * scale;
    }
}
```

### 1E. Sector Renderer (main.cpp)

New `renderLevelSectored(ObjMesh *mesh, TexCache *cache)`:

- **Backward compat:** if `mesh->numSectors == 0`, fall back to current `renderLevel()` behavior (load diffuse.bmp + lightmap.bmp, same UVs for both)
- Per sector: bind diffuse (GL_REPEAT) on unit 0, bind lightmap (GL_CLAMP_TO_EDGE) on unit 1
- Per vertex: `MT_MultiTexCoord2f(TEXTURE0, tilingU, tilingV)` + `MT_MultiTexCoord2f(TEXTURE1, lightmapU, lightmapV)`
- One `glBegin/glEnd` per sector (~8-10 batches, negligible overhead)

### 1F. Wiring in main.cpp

Replace:
- `GLuint texDiffuse/texLightmap` globals -> `TexCache texCache`
- `renderLevel(&level, texDiffuse, texLightmap)` -> `renderLevelSectored(&level, &texCache)`
- Cleanup: `texCacheFree(&texCache)`

---

## Phase 2: Blender Export Plugin

### Single file: `soob_export.py` (~400-600 lines Python)

Registers under `File > Export > SOOB Level (.obj)`.

### Export dialog options:
- Lightmap resolution: 128 / 256 / 512 (default 256)
- Lightmap UV map name (default "Lightmap") -- which UV map goes into OBJ `vt`
- Auto-create lightmap UVs (Smart UV Project if missing)
- Export path + level name

### Per-material panel (Material Properties tab):
- Tile Scale: float (default 1.0) -- written to MTL as `# tile_scale`

### Export steps:
1. **Validate** -- ensure mesh has materials and a lightmap UV map
2. **Bake per-sector lightmaps** -- for each material: create temp image, select assigned faces, bake Diffuse (Direct+Indirect only), save as `<material>_lm.bmp`
3. **Export diffuse textures** -- find Image Texture node in each material's shader, save/convert to 24-bit BMP
4. **Write OBJ** -- standard format with `mtllib`, `usemtl` per material group, `vt` from lightmap UV map
5. **Write MTL** -- `newmtl`, `map_Kd`, `# lm_map`, `# tile_scale` per material

### File naming convention:
```
level.obj / level.mtl           -- geometry + materials
brick.bmp, concrete.bmp         -- tiling diffuse textures (shared across sectors)
hallway_lm.bmp, room_a_lm.bmp  -- per-sector lightmaps
```

---

## Phase 3: Documentation

Update `leveldes.md`:
- New material-based workflow (assign materials = define sectors)
- Plugin installation + usage
- Tile scale property
- File naming conventions
- Backward compat note (no materials = old behavior)

---

## Implementation Order

1. `obj_loader.h`: Material/Sector structs + MTL parsing + usemtl + objBuildSectors
2. `texture.h`: TexCache + wrap mode parameter refactor
3. `main.cpp`: computeTilingUV + renderLevelSectored + wiring + backward compat
4. Test with hand-crafted 2-sector OBJ+MTL (two rooms, different textures)
5. Verify backward compat with original test_level.obj (no mtllib)
6. `soob_export.py`: OBJ+MTL export (no baking yet)
7. `soob_export.py`: Per-sector lightmap baking
8. `leveldes.md`: Updated docs

## Files Modified

- `obj_loader.h` -- Material/Sector structs, MTL parser, usemtl, sector building
- `texture.h` -- TexCache, wrap mode param
- `main.cpp` -- computeTilingUV, renderLevelSectored, TexCache init/free

## New Files

- `soob_export.py` -- Blender addon

## Verification

- **Sector loading:** Hand-craft a 2-material OBJ+MTL, print parsed materials/sectors
- **Texture cache:** Load 3 BMPs, verify different GL IDs; load same twice, verify cache hit
- **Tiling UVs:** Known vertex positions + normals -> verify correct projection axis
- **Full render:** 2-sector level with different diffuse tiles + separate lightmaps
- **Backward compat:** Original test_level.obj renders identically to current build
- **Blender round-trip:** Export from plugin, load in engine, verify everything

## Compatibility Notes

- All structs use fixed-size arrays, no STL -- C++98 / GCC 3.x safe for Win98
- `fabsf` may need `(float)fabs()` on old GCC -- check Win98 build
- Physics must load before sector sorting (ordering constraint in main.cpp)
- Box mapping has seams on 45-degree surfaces -- acceptable for retro aesthetic
