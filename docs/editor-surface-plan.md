# SOOB Level Editor — per-face surface plan

Today a face picks a **material**, and the material carries both *which texture
to bind* and *how to project it* (`tilingScale`, `tilingOffsetX/Y`). This doc is
the design + milestone plan for splitting those apart: material keeps the bind
state, and the **projection moves to the face**, the way Quake/Radiant-style
editors have always done it.

All editor code lives under **`editor/`**. The engine-side pieces this touches
(`obj_loader.h`, `render_level.h`) stay at the engine root because `main.cpp`
uses them too — see `docs/editor-modeling-plan.md` for the layout rule.

## Where this sits relative to what already exists

Two thirds of the work is already done, which is what makes this cheap:

- **Material is already per-face.** `EditFace.materialId` (`edit_mesh.h:43`) and
  `Triangle.materialId` (`obj_loader.h:28`); the M5 face panel assigns it to the
  selection (`onMaterialChosen`, `editor.cpp:1462`). Nothing here changes that.
- **Tiling is already computed per-vertex, on the CPU.** `computeTilingUV`
  (`render_level.h:134`) runs inside the `glBegin`/`glEnd` loop
  (`render_level.h:336`). The scale/offset it reads are plain locals hoisted out
  of the triangle loop — *not* GL state.
- **Undo already snapshots faces wholesale** — `memcpy(dst->faces, src->faces,
  nf * sizeof(EditFace))` (`edit_undo.h:38`). New face fields ride along free.

## The problem this actually solves

Not ergonomics — the **32-slot cap**. `OBJ_MAX_MATERIALS` and `OBJ_MAX_SECTORS`
are both 32 (`obj_loader.h:10-11`).

Today "same texture, different scale on this one wall" forces a duplicate
material. Each duplicate burns a material slot *and* a sector slot. A handful of
tiling variations on `concrete` exhausts the budget on a level that only ever
references six real textures. Per-face surface makes that cost zero: one
`concrete` material, any number of surface treatments.

## What it costs at render time — nearly nothing

Sectors batch **texture binds**. Tiling is not bind state, so sector batching is
unaffected: same sectors, same binds, same draw-call count. The only change is
reading the values off the triangle instead of off a hoisted local:

```c
for (int i = sec->triStart; i < sec->triStart + sec->triCount; i++) {
    Triangle *t = &mesh->tris[i];
    const Surface *sf = t->surf.scaleU != 0.0f ? &t->surf : &matDefault;
    /* ... computeTilingUV(v, &faceN, sf, &du, &dv) per vertex, as today */
```

One extra field load per triangle, inside a loop already issuing immediate-mode
`glVertex3f` per vertex — unmeasurable on a P4. `Triangle` grows 40 → 60 bytes;
at the ~5000-tri scale `objBuildSectors` is written for (`obj_loader.h:316`),
that is ~100 KB. It does **not** touch `flashlight.h`, which rasterizes into the
*lightmap* UV set, not the box-mapped diffuse UVs.

## Design decisions

**D1 — The split.** Material keeps everything that drives a GL bind; the face
gets everything that is pure CPU projection math.

| | scope | why |
|---|---|---|
| `diffusePath`, `lightmapPath`, `alphaTest`, `alphaRef` | **material** | bind state — what sectors exist to batch |
| `scaleU/V`, `offU/V`, `rot` | **face** | per-vertex CPU math, no state |

**D2 — Widen the model now, not twice.** Today's model is one uniform `scale` +
2D offset. Since the on-disk format changes anyway, go to the Radiant set in the
same pass — separate U/V scale plus a **rotation**:

```c
typedef struct {
    float scaleU, scaleV;   /* 0 = inherit the material default (sentinel) */
    float offU, offV;
    float rot;              /* degrees, within the projection plane */
} Surface;
```

Rotation is the one that can't be faked with the other two, and it is exactly
what angled walls need. 3 floats → 5 costs nothing now and saves a second format
migration later.

**D3 — `scaleU == 0` means "inherit".** Not a separate flag. Every existing
level, every Blender export, and every zero-initialized `Triangle` therefore
keeps rendering off the material default with no migration step. A face only
gets its own copy when the user actually edits it — seeded from the material's
values at that moment, so the first edit is never a visible jump.

**D4 — MTL keeps `# tile_scale` / `# tile_offset` as the material *default*.**
It stays the source of truth for the Blender authoring path
(`docs/level-design.md`), which has no way to express a per-face scale. Nothing
in that pipeline changes.

**D5 — OBJ carries the override as sticky state, like `usemtl`.** `objLoad`'s
parse loop is an if/else-if chain and silently ignores unrecognized lines
(`obj_loader.h:272-300`), so this is compatible in both directions — old loaders
skip it, Blender exports simply never emit it:

```
usemtl concrete
# f_surf 0.5 0.5 0.25 0 0
f 1//1 2//1 3//1
f 3//1 4//1 1//1
```

Sticky rather than per-face keeps the file small: one line per *run* of
same-surface faces, not one per face, and it mirrors a rule the format already
has.

**D6 — Rotation applies within the box-mapping plane.** `computeTilingUV` snaps
the projection axis to XZ / ZY / XY from the face normal (`render_level.h:137-153`).
`rot` spins the UV inside whichever plane got picked — the same thing Radiant
does, and what makes angled geometry look right. The axis-snap itself is
unchanged.

**D7 — `objAddTri` takes one trailing `const Surface *`.** It is already at 10
arguments; five more would be unreadable. `NULL` = inherit, which is what
`editMeshBuild` passes until SM1.

## Formats

- **Native `.lvl`** — the face line gains the five floats after `materialId`
  (`edit_io.h:48-53`). Loader accepts both arities, so old `.lvl` files open
  unchanged: `f <nv> <v...> <mat> [su sv ou ov rot]`.
- **OBJ + MTL** — MTL unchanged (material default); OBJ gains the sticky
  `# f_surf` line per D5.

## Module layout

| File | Change |
|---|---|
| `obj_loader.h` (engine) | `struct Surface`; `Triangle.surf`; `objAddTri` trailing param (**D7**); parse `# f_surf` as sticky state in `objLoad`. |
| `render_level.h` (engine) | `computeTilingUV` takes `const Surface *` and applies `rot` + split U/V scale (**D2**, **D6**); `renderLevelSectored` resolves per-triangle vs material default (**D3**). |
| `editor/edit_mesh.h` | `EditFace.surf`; `editAddFace` zero-inits it (= inherit). |
| `editor/edit_mesh_build.h` | Pass `&f->surf` through to both triangles of a quad. |
| `editor/edit_io.h` | `.lvl` read/write both arities; OBJ export emits `# f_surf` runs. |
| `editor/editor.cpp` | Panel rework: tiling spinners become face-scope and write every selected face; add rotation + split-scale widgets; `onTilingChanged` seeds from material on first edit. |
| `editor/edit_undo.h` | **No change** — `memcpy` of `EditFace` already covers it. |
| `editor/edit_io_test.cpp`, `edit_mesh_test.cpp` | Round-trip cases for both `.lvl` arities and the sticky-run OBJ writer. |

## Milestones

| M | Deliverable | Status |
|---|---|---|
| **SM0** | **Engine plumbing, no behaviour change.** `Surface` + `Triangle.surf` + `objAddTri` param + `computeTilingUV` rewrite (split scale, rotation) + the inherit resolve in `renderLevelSectored`. Everything passes `NULL`/zero, so every existing level renders **byte-identically**. | **done** — verified bit-exact against the pre-SM0 formula, not by eye. |
| **SM1** | **Face carries it.** `EditFace.surf`, propagated through `editMeshBuild`; `.lvl` round-trip with the extended face line; undo rides free. Still driven by the existing material-scope spinners. | **done** — extrude carries the surface too (matching how it already carries the material). |
| **SM2** | **Panel rework.** Tiling spinners move to face scope and write all selected faces; rotation + split-U/V widgets added; first edit seeds from the material (**D3**). | **done** — plus a `Use Material Tiling` button, since inherit must stay reachable. |
| **SM3** | **OBJ export/import.** Sticky `# f_surf` runs on write, sticky parse on read; round-trip a Blender bake to confirm the lightmap path is untouched. | **done** — a mesh nobody re-tiled still exports zero `# f_surf` lines. |
| polish | **Copy/paste surface** between faces (Radiant's middle-click surface-copy), fit-to-face, align-to-neighbour. | |

**Out:** per-face *lightmap* UVs (still baked in Blender), per-vertex UVs, a UV
editor, per-face alpha-test or texture overrides (those are bind state — D1).

## Settled during implementation

- **D8 — inherit must be reachable, not just leavable.** The scale spinners
  cannot reach the `0` sentinel (their range starts at 0.01), so leaving a face
  inheriting would be a one-way door. SM2 adds a **Use Material Tiling** button
  that calls `editSurfClear` on the selection. Not in the original plan; the
  D3 sentinel does not work as a user-facing state without it.
- **D9 — extrude carries the surface.** `editExtrude` already propagated the
  source face's `materialId` to new side faces; propagating `materialId` but not
  `surf` would make an extrude off a re-tiled wall snap back to the material
  default. `esurf[]` mirrors the existing `emat[]`.
- **`usemtl` does not reset the sticky surface.** The two are independent
  sticky states in `objLoad`, and the writer in `edit_io.h` matches. Resetting
  on `usemtl` would be equally defensible, but only if both sides agree — this
  is the coupling to preserve if either is ever rewritten.
- **Changing a face's material leaves its surface alone.** An explicitly-tiled
  face keeps its tiling when reassigned to another material; only inheriting
  faces follow the new material's default. Explicit beats implicit, and the
  alternative silently discards work.

## Spine

Material is already per-face; tiling is already per-vertex CPU math; undo already
copies faces wholesale. So this is mostly a **struct move plus a format
migration**, not new machinery. The inherit sentinel (**D3**) is what keeps it
free of a migration step, and SM0 landing as a provable no-op is what keeps the
engine-side change honest. The real payoff is escaping the 32-material /
32-sector cap, with the Radiant surface model (**D2**) arriving in the same
format change rather than a second one later.
