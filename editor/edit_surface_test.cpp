/*
 * edit_surface_test.cpp — SM2 check: per-face surface semantics through the
 * REAL render path (editMeshBuild -> the resolve renderLevelSectored uses ->
 * computeTilingUV). No FLTK, so it runs headless.
 *
 * The claim being tested is D3 from docs/editor-surface-plan.md: the panel
 * seeds its spinners from the face's *effective* surface, so the first edit --
 * the one that stops a face inheriting -- must change nothing on screen except
 * the single value the user touched. That is only true if seeding is an exact
 * no-op, which is what part 1 proves bit-for-bit.
 *
 *   g++ -DWIN32 -I. -I../SOOB-Core editor/edit_surface_test.cpp -o est -lopengl32
 * (Linux: drop -DWIN32/-lopengl32 and the windows.h include is skipped.)
 */
#ifdef WIN32
#include <windows.h>
#endif
#include <GL/gl.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cmath>

static void conLogf(const char *fmt, ...)
{ va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); }

#include "obj_loader.h"
#include "texture.h"
#include "render_level.h"
#include "edit_mesh.h"
#include "edit_mesh_build.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { \
    printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

static int bits(float f) { int i; memcpy(&i, &f, 4); return i; }

/* Mirror of the resolve in renderLevelSectored: a triangle's own surface, or
   the sector material's default while it is still inheriting. */
static Surface resolveTri(ObjMesh *o, Triangle *t, int matId)
{
    if (t->surf.scaleU != 0.0f) return t->surf;
    Surface s;
    s.scaleU = s.scaleV = 1.0f; s.offU = s.offV = 0.0f; s.rot = 0.0f;
    if (matId >= 0 && matId < o->numMaterials) {
        s.scaleU = s.scaleV = o->materials[matId].tilingScale;
        s.offU   = o->materials[matId].tilingOffsetX;
        s.offV   = o->materials[matId].tilingOffsetY;
    }
    return s;
}

/* Every UV the mesh would draw, in triangle order. Returns the count. */
static int gatherUVs(EditMesh *m, float *out, int cap)
{
    ObjMesh o; objInit(&o);
    editMeshBuild(m, &o);
    int n = 0, s, i, j;
    for (s = 0; s < o.numSectors; s++) {
        Sector *sec = &o.sectors[s];
        for (i = sec->triStart; i < sec->triStart + sec->triCount; i++) {
            Triangle *t = &o.tris[i];
            Surface sf = resolveTri(&o, t, sec->materialId);
            Vec3 fn;
            if (o.numNormals > 0 && t->n[0] >= 0 && t->n[0] < o.numNormals) fn = o.normals[t->n[0]];
            else { fn.x = 0; fn.y = 1; fn.z = 0; }
            for (j = 0; j < 3; j++) {
                if (n + 2 > cap) { objFree(&o); return n; }
                computeTilingUV(&o.verts[t->v[j]], &fn, &sf, &out[n], &out[n + 1]);
                n += 2;
            }
        }
    }
    objFree(&o);
    return n;
}

static int sameBits(const float *a, const float *b, int n)
{
    int i;
    for (i = 0; i < n; i++) if (bits(a[i]) != bits(b[i])) return 0;
    return 1;
}

/* What the SM2 panel seeds its spinners with for an inheriting face -- must
   match EditorView::materialSurf / faceEffectiveSurf in editor.cpp. */
static Surface materialSurf(EditMesh *m, int id)
{
    Surface s;
    s.scaleU = s.scaleV = 1.0f; s.offU = s.offV = 0.0f; s.rot = 0.0f;
    if (id >= 0 && id < m->numMats) {
        s.scaleU = s.scaleV = m->mats[id].tilingScale;
        s.offU   = m->mats[id].tilingOffsetX;
        s.offV   = m->mats[id].tilingOffsetY;
    }
    return s;
}

#define CAP 4096
int main(void)
{
    EditMesh m; editMeshInit(&m);
    memset(&m.mats[0], 0, sizeof(Material));
    strcpy(m.mats[0].name, "concrete");
    m.mats[0].tilingScale   = 0.6f;      /* deliberately not 1.0 */
    m.mats[0].tilingOffsetX = 2.5f;
    m.mats[0].tilingOffsetY = 0.0f;
    m.numMats = 1;
    editAddCube(&m, 0, 0, 0, 2, 2, 2, 0);

    static float base[CAP], seeded[CAP], edited[CAP], cleared[CAP];
    int n = gatherUVs(&m, base, CAP);
    CHECK(n > 0);
    printf("cube: %d faces, %d UV floats gathered\n", m.numFaces, n);

    /* --- part 1: seeding is an exact no-op (D3) ------------------------- */
    /* Panel opens on face 0: it seeds from the effective surface, then the user
       nudges nothing. onTilingChanged writes all five back. Screen must not
       move -- not "look similar", but produce identical floats. */
    m.faces[0].surf = materialSurf(&m, m.faces[0].materialId);
    CHECK(gatherUVs(&m, seeded, CAP) == n);
    CHECK(sameBits(base, seeded, n));
    printf("part 1 (seed is a bit-exact no-op): %s\n",
           sameBits(base, seeded, n) ? "PASS" : "FAIL");

    /* The face really did stop inheriting -- otherwise part 1 is vacuous. */
    CHECK(m.faces[0].surf.scaleU != 0.0f);

    /* --- part 2: an edit changes THIS face and no other ----------------- */
    m.faces[0].surf.scaleU = 1.2f;
    CHECK(gatherUVs(&m, edited, CAP) == n);
    CHECK(!sameBits(base, edited, n));               /* something moved */
    /* A cube face is 1 quad = 2 tris = 6 UV pairs = 12 floats. Face 0 comes
       first in build order, so only the first 12 floats may differ. */
    CHECK(!sameBits(base, edited, 12));              /* face 0 moved ... */
    CHECK(sameBits(base + 12, edited + 12, n - 12)); /* ... and nothing else */
    printf("part 2 (edit is local to the face): %s\n",
           sameBits(base + 12, edited + 12, n - 12) ? "PASS" : "FAIL");

    /* --- part 3: "Use Material Tiling" returns to inherit --------------- */
    editSurfClear(&m.faces[0].surf);
    CHECK(m.faces[0].surf.scaleU == 0.0f);
    CHECK(gatherUVs(&m, cleared, CAP) == n);
    CHECK(sameBits(base, cleared, n));
    printf("part 3 (inherit is reachable again): %s\n",
           sameBits(base, cleared, n) ? "PASS" : "FAIL");

    /* --- part 4: split scale and rotation actually reach the renderer --- */
    {
        EditMesh q; editMeshInit(&q);
        memset(&q.mats[0], 0, sizeof(Material));
        q.mats[0].tilingScale = 1.0f; q.numMats = 1;
        editAddCube(&q, 0, 0, 0, 2, 2, 2, 0);
        static float a[CAP], b[CAP];
        int qn = gatherUVs(&q, a, CAP);

        q.faces[0].surf.scaleU = 1.0f; q.faces[0].surf.scaleV = 4.0f;
        q.faces[0].surf.offU = q.faces[0].surf.offV = 0.0f;
        q.faces[0].surf.rot = 0.0f;
        CHECK(gatherUVs(&q, b, CAP) == qn);
        CHECK(bits(a[0]) == bits(b[0]));             /* U untouched by scaleV */
        CHECK(bits(a[1]) != bits(b[1]) || a[1] == 0.0f);

        q.faces[0].surf.scaleV = 1.0f; q.faces[0].surf.rot = 90.0f;
        CHECK(gatherUVs(&q, b, CAP) == qn);
        CHECK(fabs(b[0] - (-a[1])) < 1e-5);          /* (u,v) -> (-v,u) */
        CHECK(fabs(b[1] - ( a[0])) < 1e-5);
        printf("part 4 (split scale + rotation reach the renderer): ok\n");
        editMeshFree(&q);
    }

    editMeshFree(&m);
    printf(failures ? "\n%d FAILURE(S)\n" : "\nALL PASS\n", failures);
    return failures ? 1 : 0;
}
