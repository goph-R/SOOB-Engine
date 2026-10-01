/*
 * SOOB Level Editor — skeleton
 * ============================
 *
 * A native Win98-capable level editor for the SOOB / SDLFun engine, built on
 * FLTK 1.3 (fixed-function GL, no C++11, no shaders — same envelope as the
 * game). It renders levels through the EXACT engine code the game ships:
 *
 *     editLoadLevel()  (edit_load.h)     -> build a Game with just the
 *                                            renderable state (no physics/
 *                                            nav/scripts/flashlight)
 *     renderWorld()    (render_world.h)  -> lit level + entities, lightmaps
 *                                            and all, from a free-fly Camera
 *     render_level.h                     -> the shared GL draw path itself
 *
 * On top of that it draws an editor-only overlay (ground grid + origin axis
 * gizmo) — the seed for gizmos / selection / entity handles later.
 *
 * The lit level mesh (its own diffuse + baked lightmap) renders through the
 * engine path, and entity meshes resolve too: editLoadAssets (edit_assets.h)
 * runs assets.lua in a bare Lua state and registers its models + textures into
 * the AssetRegistry, so entLoadFile can map mesh=/iqm=/tex= names to files.
 * No UI/audio/script runtime is pulled in — just Lua.
 *
 * Run from the repo root (assets are relative-pathed, exactly like the game):
 *     ./soob_editor [level.obj] [level.ent]
 * defaults to assets/levels/test_level.{obj,ent}.
 *
 * Controls: right-drag = look, WASD = move, PgUp/PgDn = up/down, wheel = dolly,
 * left-click = select, Shift+left-click = add/toggle, 1/2/3 = vertex/edge/face,
 * G = grab/move (X/Y/Z axis-lock, click/Enter confirm, Esc cancel),
 * E = extrude faces, F = make face (3-4 verts), X/Del = delete,
 * Ctrl+Z / Ctrl+Y = undo / redo.
 * Menus: File (Save / Open / Export OBJ), Add (Cube/Plane),
 * Mesh (Extrude / Make Face / Flip Normals / Delete).
 */

#ifdef _WIN32
#include <windows.h>          /* wglGetProcAddress + APIENTRY, ahead of GL */
#endif

#include <FL/Fl.H>
#include <FL/Fl_Gl_Window.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Value_Input.H>
#include <FL/Fl_Pixmap.H>
#include <FL/fl_ask.H>
#include <FL/Fl_File_Chooser.H>
#include <FL/gl.h>
#include "edit_dpi.h"
#include "edit_menupad.h"

#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <cmath>

/* Toolbar select-mode icons — embedded XPM pixmaps. Draw them in
   editor/icons/*.xpm (keep the array names). */
#include "icons/mode_vert.xpm"
#include "icons/mode_edge.xpm"
#include "icons/mode_face.xpm"
#include "icons/mode_entity.xpm"

/* Boot-splash art (GIMP-exported XPM). GIMP names the array after the source
   file's full path; it was normalised to `splash_xpm` by hand (the raw name is
   not a valid C identifier). Draw a new splash in editor/images/splash.xpm and
   keep that array name. */
#include "images/splash.xpm"

/* The engine's header-only modules all call conLogf() (the game routes it to
   its dev console). The editor has no console, so provide a plain stdout sink
   BEFORE including any engine header. */
static void conLogf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    /* Flush every line. Redirected stdout is block-buffered, so without this a
       crash during startup discards the whole buffer and `SoobEditor > log.txt`
       comes back empty -- which is exactly the case you most need it for. */
    fflush(stdout);
}

/* Engine modules — struct field types first, then the shared render/load
   headers. Deliberately NOT included: ui.h, sound.h, music.h, script.h — the
   game.h/game_session.h split keeps those out of the editor's TU. */
#include "obj_loader.h"
#include "texture.h"          /* SOOB-Core */
#include "iqm.h"
#include "asset_registry.h"   /* SOOB-Core */
#include "entity.h"
#include "flashlight.h"
#include "physics.h"
#include "nav.h"
#include "path.h"
#include "game.h"             /* struct Game only (session lifecycle omitted) */
#include "render_level.h"
#include "render_world.h"
#include "edit_load.h"
#include "edit_assets.h"     /* minimal assets.lua -> AssetRegistry (models+textures) */
#include "edit_mesh.h"       /* editor native mesh (verts/faces, 1 cm snap)          */
#include "edit_mesh_build.h" /* EditMesh -> ObjMesh for the engine renderer          */
#include "edit_select.h"     /* vert/edge/face selection state                       */
#include "edit_pick.h"       /* screen-space + ray picking (no GLU)                  */
#include "edit_undo.h"       /* whole-mesh snapshot undo/redo                        */
#include "edit_history.h"    /* unified mesh+entity tagged undo (supersedes above)   */
#include "edit_ops.h"        /* extrude                                              */
#include "edit_io.h"         /* save/load .lvl + OBJ/MTL export                      */

/* GL proc loader for initMultitexture(). On Win98/MinGW the ARB multitexture
   entry points are resolved at runtime; on Linux they're linked directly and
   the loader is ignored (pass NULL). wglGetProcAddress returns PROC (__stdcall,
   not void*), so wrap it to match the GLProcLoader signature. */
#ifdef _WIN32
static void *editGLGetProc(const char *name)
{
    return (void *)wglGetProcAddress(name);
}
#endif

/* ---- Boot splash ----------------------------------------------------------
 * A frameless window (image + a status line) shown on top of the main window
 * while the first-frame load runs. On the Win98/P3 target that load (level OBJ
 * + texture uploads) is slow enough that per-phase feedback is real, not a
 * flash. bootstrap() ticks the status line through its phases via
 * splashStatus(); main() owns the window's lifetime and hides it when done. */
static Fl_Window *gSplash     = 0;
static Fl_Box    *gSplashText = 0;

static void splashStatus(const char *msg)
{
    if (!gSplashText) return;
    gSplashText->copy_label(msg);
    gSplashText->redraw();
    Fl::check();                 /* pump so the label repaints mid-load */
}

class EditorView : public Fl_Gl_Window {
public:
    const char   *objPath;
    const char   *entPath;

    Game          scene;
    AssetRegistry assetReg;
    int           loaded;      /* level loaded OK */
    int           bootstrapped; /* GL init + load done (needs live context) */
    int           booting;      /* re-entry guard while bootstrap() runs */

    /* M0: the editor's own mesh (a demo cube for now) rendered through the
       new EditMesh -> ObjMesh -> engine path, alongside the loaded level. */
    EditMesh      emesh;
    ObjMesh       eobj;
    int           haveEmesh;

    EditSelection sel;          /* M1: vert/edge/face selection */
    int           pushX, pushY; /* mouse-down point, for click-vs-drag */

    /* Toolbar mode buttons (radio); kept in sync with sel.mode both ways. */
    Fl_Button    *bVert, *bEdge, *bFace, *bEnt;

    /* EM0: entity selection (mode SEL_ENTITY). Entities aren't part of the mesh,
       so this lives on the view — NOT in EditSelection, which is freed/rebuilt on
       every topology edit. One flag per entity slot (index == identity). */
    unsigned char entSel[MAX_ENTITIES];

    DocHistory    hist;         /* EM1: unified mesh + entity snapshot undo/redo */
    Fl_Menu_Bar  *menuBar;      /* Edit menu, for greying out Undo/Redo */
    int           undoIdx, redoIdx;

    /* Property-panel widgets. faceGroup (material + tiling) and vertGroup
       (X/Y/Z) overlap; refreshPanel shows whichever fits the mode + selection. */
    Fl_Group       *propPanel, *faceGroup, *vertGroup;
    Fl_Choice      *matChoice;
    Fl_Input       *diffuseInput;
    Fl_Value_Input *scaleInput, *offXInput, *offYInput;
    Fl_Value_Input *vxInput, *vyInput, *vzInput;
    int             panelVert;       /* the vertex the X/Y/Z fields edit, or -1 */
    int             vertEditPushed;  /* one undo push per vertex-edit gesture */

    /* EM3: entity property form (entGroup). Common fields for any single-
       selected entity; the light-only fields live in a nested group that
       refreshEntPanel reveals for ENT_LIGHT. */
    Fl_Group       *entGroup, *entLightGroup;
    Fl_Input       *entNameIn, *entGroupIn;
    Fl_Box         *entTypeBox;
    Fl_Value_Input *entXIn, *entYIn, *entZIn, *entRotIn, *entScaleIn;
    Fl_Value_Input *entLrIn, *entLgIn, *entLbIn, *entLiIn, *entLradIn;
    int             panelEnt;        /* the entity the form edits, or -1 */
    int             entEditPushed;   /* one undo push per entity-edit gesture */

    /* EM3b: per-type field sub-groups (overlap; refreshEntPanel shows one). */
    Fl_Group        *gItem, *gEnemy, *gPlatform, *gSwitch, *gTrigger, *gDoor, *gPath;
    Fl_Choice       *itType;
    Fl_Value_Input  *enHealth, *enSpeed, *enSight;
    Fl_Input        *plPath;    Fl_Choice *plMove;    Fl_Value_Input *plSpeed;
    Fl_Check_Button *plEnabled, *plFace;
    Fl_Input        *swTarget;
    Fl_Value_Input  *trSx, *trSy, *trSz;   Fl_Input *trTarget;   Fl_Check_Button *trOnce;
    Fl_Choice       *drMotion, *drAxis;    Fl_Value_Input *drAmount, *drSpeed, *drAuto;
    Fl_Value_Input  *paOrder;

    /* M2 grab (modal move): active while `grabbing`. The affected verts and
       their pre-grab positions are captured at start; the mouse delta
       (optionally axis-locked) moves them, snapped to 1 cm. */
    int   grabbing, grabAxis;   /* grabAxis: -1 none, 0/1/2 = X/Y/Z */
    int   grabAnchorX, grabAnchorY, grabCurX, grabCurY;
    Vec3  grabCentroid;
    int  *grabVerts; Vec3 *grabOrig; int nGrab;
    int   suppressRelease;      /* skip the select on a grab-confirm click */
    int   grabFromExtrude;      /* grab launched by extrude (cancel = full undo) */
    int   grabIsEnt;            /* EM1: grab is moving entities, not mesh verts */
    int   rotating;             /* EM2: modal yaw-rotate of selected entities */
    float *grabOrigYaw;         /* EM2: pre-rotate rotY per gathered entity */

    /* Free-fly camera: eye position + yaw/pitch (radians). */
    float camX, camY, camZ;
    float yaw, pitch;
    int   lastX, lastY;

    EditorView(int X, int Y, int W, int H)
        : Fl_Gl_Window(X, Y, W, H),
          objPath("assets/levels/test_level.obj"),
          entPath("assets/levels/test_level.ent"),
          loaded(0), bootstrapped(0), booting(0), haveEmesh(0), pushX(0), pushY(0),
          bVert(0), bEdge(0), bFace(0), bEnt(0),
          menuBar(0), undoIdx(-1), redoIdx(-1),
          propPanel(0), faceGroup(0), vertGroup(0),
          matChoice(0), diffuseInput(0), scaleInput(0), offXInput(0), offYInput(0),
          vxInput(0), vyInput(0), vzInput(0), panelVert(-1), vertEditPushed(0),
          entGroup(0), entLightGroup(0), entNameIn(0), entGroupIn(0), entTypeBox(0),
          entXIn(0), entYIn(0), entZIn(0), entRotIn(0), entScaleIn(0),
          entLrIn(0), entLgIn(0), entLbIn(0), entLiIn(0), entLradIn(0),
          panelEnt(-1), entEditPushed(0),
          gItem(0), gEnemy(0), gPlatform(0), gSwitch(0), gTrigger(0), gDoor(0), gPath(0),
          itType(0), enHealth(0), enSpeed(0), enSight(0),
          plPath(0), plMove(0), plSpeed(0), plEnabled(0), plFace(0),
          swTarget(0), trSx(0), trSy(0), trSz(0), trTarget(0), trOnce(0),
          drMotion(0), drAxis(0), drAmount(0), drSpeed(0), drAuto(0), paOrder(0),
          grabbing(0), grabAxis(-1),
          grabAnchorX(0), grabAnchorY(0), grabCurX(0), grabCurY(0),
          grabVerts(0), grabOrig(0), nGrab(0), suppressRelease(0), grabFromExtrude(0),
          grabIsEnt(0), rotating(0), grabOrigYaw(0),
          camX(0.0f), camY(2.0f), camZ(6.0f),
          yaw(-1.5708f), pitch(-0.15f), lastX(0), lastY(0)
    {
        mode(FL_RGB | FL_DEPTH | FL_DOUBLE);
        docHistInit(&hist);
        memset(entSel, 0, sizeof(entSel));
    }

    /* eye + look-at target from the free-fly camera state. */
    void computeCamera(Camera *cam)
    {
        cam->rollDeg = 0.0f;
        cam->eye.x = camX; cam->eye.y = camY; cam->eye.z = camZ;
        float cp = cosf(pitch);
        cam->at.x = camX + cosf(yaw) * cp;
        cam->at.y = camY + sinf(pitch);
        cam->at.z = camZ + sinf(yaw) * cp;
    }

    void initGL()
    {
        {   /* Logged here rather than in main(): this runs inside the first
               live frame, where the context really is current. */
            const char *vend = (const char *)glGetString(GL_VENDOR);
            const char *rend = (const char *)glGetString(GL_RENDERER);
            const char *vers = (const char *)glGetString(GL_VERSION);
            conLogf("editor: GL %s | %s | %s\n",
                    vend ? vend : "(none)", rend ? rend : "(none)",
                    vers ? vers : "(none)");
        }
#ifdef _WIN32
        initMultitexture(editGLGetProc);
#else
        initMultitexture(NULL);   /* Linux: uses linked ARB symbols */
#endif
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glEnable(GL_CULL_FACE);

        /* Fallback GL_LIGHT0 (matches the game's init) — lightmapped sectors
           disable lighting themselves; this lights entities and any
           un-lightmapped geometry. renderWorld sets its POSITION each frame. */
        glEnable(GL_LIGHT0);
        GLfloat amb[] = { 0.30f, 0.30f, 0.30f, 1.0f };
        GLfloat dif[] = { 0.85f, 0.85f, 0.85f, 1.0f };
        glLightfv(GL_LIGHT0, GL_AMBIENT, amb);
        glLightfv(GL_LIGHT0, GL_DIFFUSE, dif);
    }

    /* One-time GL init + asset/level load. Needs a live GL context (texture
       uploads), so it runs on the first frame — or is driven explicitly from
       main() with the boot splash up, whichever comes first. Re-entry-safe: the
       Fl::check() inside splashStatus() can pump a draw() mid-load, and the
       `booting` guard makes that draw() a no-op instead of a second load. */
    void bootstrap()
    {
        if (bootstrapped || booting) return;
        booting = 1;
        make_current();               /* context may not be current when main() calls us */
        initGL();

        splashStatus("Loading assets...");
        assetRegInit(&assetReg);
        editLoadAssets(&assetReg, "assets.lua");  /* resolve entity mesh/tex names */

        splashStatus("Loading level...");
        loaded = editLoadLevel(&scene, objPath, entPath, &assetReg);
        if (!loaded)
            conLogf("editor: failed to load %s / %s\n", objPath, entPath);

        /* M0: seed a hardcoded 2 m cube through the editor-mesh path. It borrows
           the loaded level's first material so it's textured with a real
           box-mapped diffuse; falls back to flat grey otherwise. */
        splashStatus("Building mesh...");
        editMeshInit(&emesh);
        if (loaded && scene.level.numMaterials > 0) {
            emesh.mats[0] = scene.level.materials[0];
        } else {
            memset(&emesh.mats[0], 0, sizeof(Material));
            strcpy(emesh.mats[0].name, "editor_default");
            emesh.mats[0].tilingScale = 1.0f;
        }
        emesh.numMats = 1;
        editAddCube(&emesh, 0.0f, 1.0f, -4.0f, 2.0f, 2.0f, 2.0f, 0);
        objInit(&eobj);
        editMeshBuild(&emesh, &eobj);
        editSelInit(&sel, &emesh);   /* M1: selection + derived edge list */
        haveEmesh = (loaded != 0);   /* renderer needs scene.texCache valid */
        rebuildMatChoice();          /* M5: fill the material dropdown */
        refreshPanel();
        conLogf("editor: demo cube built (%d tris, %d sectors, %d edges)\n",
                eobj.numTris, eobj.numSectors, sel.numEdges);

        splashStatus("Ready");
        bootstrapped = 1;
        booting = 0;
    }

    void draw()
    {
        /* First live frame: context is current, so the load is safe to run here
           if main() hasn't already driven it (see bootstrap()). */
        if (!bootstrapped) bootstrap();
        if (booting) {                 /* re-entrant draw mid-load: just clear */
            glClearColor(0.16f, 0.17f, 0.21f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            return;
        }

        int pw = w() > 0 ? w() : 1;
        int ph = h() > 0 ? h() : 1;
        glViewport(0, 0, pw, ph);

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glSetPerspective(70.0f, (float)pw / (float)ph, 0.05f, 500.0f);

        glClearColor(0.16f, 0.17f, 0.21f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        Camera cam;
        computeCamera(&cam);

        /* Set the view once so the overlay draws in world space even before a
           level is loaded; renderWorld re-establishes the same matrix. */
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glLookAt(cam.eye, cam.at);

        if (loaded)
            renderWorld(&scene, cam);

        /* M0: draw the editor mesh through the same engine path. renderWorld
           leaves GL_LIGHTING enabled; the cube has no lightmap, so it takes the
           box-mapped diffuse + GL_LIGHT0 branch. */
        if (haveEmesh) {
            glEnable(GL_LIGHTING);
            renderLevelSectored(&eobj, &scene.texCache);
            drawSelectionOverlay();
        }

        drawOverlay();
        drawEntityMarkers();
    }

    /* M1: draw the edit-mesh wireframe + the current selection. Everything is
       depth-tested so hidden parts stay hidden (matching the occlusion-aware
       picking); a small glDepthRange bias pulls the overlay just in front of
       the coincident surfaces to avoid z-fighting. Colours: dark wire, orange
       (1.0, 0.6, 0.1) selection. */
    void drawSelectionOverlay()
    {
        glDisable(GL_LIGHTING);
        glDisable(GL_TEXTURE_2D);

        /* Bias the overlay slightly toward the camera (0.999 vs 1.0) so lines,
           points and fills sit on top of the coincident geometry, while still
           being occluded by clearly-nearer faces. */
        glDepthRange(0.0, 0.999);

        /* All edges, thin + dark. */
        glLineWidth(1.0f);
        glColor3f(0.05f, 0.05f, 0.05f);
        glBegin(GL_LINES);
        for (int i = 0; i < sel.numEdges; i++) {
            Vec3 *a = &emesh.verts[sel.edges[i].a].pos;
            Vec3 *b = &emesh.verts[sel.edges[i].b].pos;
            glVertex3f(a->x, a->y, a->z);
            glVertex3f(b->x, b->y, b->z);
        }
        glEnd();

        /* Unselected verts (vertex mode only), depth-tested. */
        if (sel.mode == SEL_VERT) {
            glPointSize(4.0f * editDpiScale);
            glColor3f(0.1f, 0.1f, 0.1f);
            glBegin(GL_POINTS);
            for (int i = 0; i < emesh.numVerts; i++) {
                if (sel.vertSel[i]) continue;
                Vec3 *p = &emesh.verts[i].pos;
                glVertex3f(p->x, p->y, p->z);
            }
            glEnd();
        }

        /* Selection highlight — depth-tested (hidden parts stay hidden). */
        if (sel.mode == SEL_FACE) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDisable(GL_CULL_FACE);
            glColor4f(1.0f, 0.6f, 0.1f, 0.4f);
            glBegin(GL_TRIANGLES);
            for (int i = 0; i < emesh.numFaces; i++) {
                if (!sel.faceSel[i]) continue;
                EditFace *f = &emesh.faces[i];
                Vec3 *v0 = &emesh.verts[f->v[0]].pos;
                Vec3 *v1 = &emesh.verts[f->v[1]].pos;
                Vec3 *v2 = &emesh.verts[f->v[2]].pos;
                glVertex3f(v0->x, v0->y, v0->z);
                glVertex3f(v1->x, v1->y, v1->z);
                glVertex3f(v2->x, v2->y, v2->z);
                if (f->nv == 4) {
                    Vec3 *v3 = &emesh.verts[f->v[3]].pos;
                    glVertex3f(v0->x, v0->y, v0->z);
                    glVertex3f(v2->x, v2->y, v2->z);
                    glVertex3f(v3->x, v3->y, v3->z);
                }
            }
            glEnd();
            glEnable(GL_CULL_FACE);
            glDisable(GL_BLEND);
        } else if (sel.mode == SEL_EDGE) {
            glLineWidth(3.0f * editDpiScale);
            glColor3f(1.0f, 0.6f, 0.1f);
            glBegin(GL_LINES);
            for (int i = 0; i < sel.numEdges; i++) {
                if (!sel.edgeSel[i]) continue;
                Vec3 *a = &emesh.verts[sel.edges[i].a].pos;
                Vec3 *b = &emesh.verts[sel.edges[i].b].pos;
                glVertex3f(a->x, a->y, a->z);
                glVertex3f(b->x, b->y, b->z);
            }
            glEnd();
            glLineWidth(1.0f);
        } else if (sel.mode == SEL_VERT) {
            glPointSize(8.0f * editDpiScale);
            glColor3f(1.0f, 0.6f, 0.1f);
            glBegin(GL_POINTS);
            for (int i = 0; i < emesh.numVerts; i++) {
                if (!sel.vertSel[i]) continue;
                Vec3 *p = &emesh.verts[i].pos;
                glVertex3f(p->x, p->y, p->z);
            }
            glEnd();
            glPointSize(1.0f);
        }

        glDepthRange(0.0, 1.0);
        glEnable(GL_LIGHTING);
    }

    /* Editor-only chrome — never touches the engine renderer. Drawn in world
       space (modelview is still the view matrix here). */
    void drawOverlay()
    {
        glDisable(GL_LIGHTING);
        glDisable(GL_TEXTURE_2D);

        /* Ground grid on the XZ plane (depth-tested so it sits under geo). */
        glColor3f(0.30f, 0.30f, 0.36f);
        glBegin(GL_LINES);
        const int N = 20;
        for (int i = -N; i <= N; i++) {
            glVertex3f((float)i, 0.0f, (float)-N);
            glVertex3f((float)i, 0.0f, (float) N);
            glVertex3f((float)-N, 0.0f, (float)i);
            glVertex3f((float) N, 0.0f, (float)i);
        }
        glEnd();

        /* Origin axis gizmo: X red, Y green, Z blue. Drawn on top (no depth)
           so it's always visible as a reference. */
        glDisable(GL_DEPTH_TEST);
        glLineWidth(2.0f * editDpiScale);
        glBegin(GL_LINES);
        glColor3f(1.0f, 0.25f, 0.25f); glVertex3f(0,0,0); glVertex3f(2.0f,0,0);
        glColor3f(0.25f, 1.0f, 0.25f); glVertex3f(0,0,0); glVertex3f(0,2.0f,0);
        glColor3f(0.35f, 0.5f, 1.0f);  glVertex3f(0,0,0); glVertex3f(0,0,2.0f);
        glEnd();
        glLineWidth(1.0f);
        glEnable(GL_DEPTH_TEST);

        glEnable(GL_LIGHTING);
    }

    /* Three great-circle line loops (XZ / XY / YZ) — a cheap wire sphere for the
       light gizmo. Caller sets colour + line width. */
    void drawWireSphere(float cx, float cy, float cz, float rad, int segs)
    {
        int i; float a;
        glBegin(GL_LINE_LOOP);
        for (i = 0; i < segs; i++) { a = 6.2831853f * i / segs; glVertex3f(cx + rad*cosf(a), cy, cz + rad*sinf(a)); }
        glEnd();
        glBegin(GL_LINE_LOOP);
        for (i = 0; i < segs; i++) { a = 6.2831853f * i / segs; glVertex3f(cx + rad*cosf(a), cy + rad*sinf(a), cz); }
        glEnd();
        glBegin(GL_LINE_LOOP);
        for (i = 0; i < segs; i++) { a = 6.2831853f * i / segs; glVertex3f(cx, cy + rad*cosf(a), cz + rad*sinf(a)); }
        glEnd();
    }

    /* EM0: entity markers. In entity mode only, draw a small always-on-top gizmo
       at every active entity so meshless ones (waypoints, triggers, spawns, ...)
       are visible and clickable too — dim blue unselected, bright orange (the
       same 1.0,0.6,0.1 as the mesh selection) when selected. Never drawn in the
       geometry modes, so it doesn't clutter them. */
    void drawEntityMarkers()
    {
        if (sel.mode != SEL_ENTITY || !loaded || !scene.entities) return;
        glDisable(GL_LIGHTING);
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_DEPTH_TEST);          /* markers always visible, even behind walls */
        int n = scene.entities->count;
        for (int i = 0; i < n; i++) {
            Entity *e = &scene.entities->entities[i];
            if (!e->active) continue;
            int on = entSel[i];
            float s = on ? 0.40f : 0.25f;
            if (on) { glLineWidth(2.5f * editDpiScale); glColor3f(1.0f, 0.6f, 0.1f); }
            else    { glLineWidth(1.0f); glColor3f(0.55f, 0.8f, 1.0f); }
            float x = e->posX, y = e->posY, z = e->posZ;
            glBegin(GL_LINES);                          /* 3-axis cross */
            glVertex3f(x - s, y, z); glVertex3f(x + s, y, z);
            glVertex3f(x, y - s, z); glVertex3f(x, y + s, z);
            glVertex3f(x, y, z - s); glVertex3f(x, y, z + s);
            glEnd();
            glBegin(GL_LINE_LOOP);                      /* footprint square (XZ) */
            glVertex3f(x - s, y, z - s); glVertex3f(x + s, y, z - s);
            glVertex3f(x + s, y, z + s); glVertex3f(x - s, y, z + s);
            glEnd();
            if (e->type == ENT_LIGHT) {                 /* EM3: reach + colour */
                float lr = e->light.r, lg = e->light.g, lb = e->light.b;
                float mxc = lr > lg ? lr : lg; if (lb > mxc) mxc = lb;
                if (mxc < 0.25f) { lr += 0.25f; lg += 0.25f; lb += 0.25f; }  /* stay visible */
                glColor3f(lr, lg, lb);
                glLineWidth(on ? 2.0f * editDpiScale : 1.0f);
                drawWireSphere(x, y, z, e->light.radius, 24);
            }
        }
        glLineWidth(1.0f);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_LIGHTING);
    }

    /* Poll held movement keys (called from the frame timer). Returns 1 if the
       camera moved (so the caller can redraw). */
    int moveFromKeys()
    {
        const float speed = 0.15f;
        Camera cam;
        computeCamera(&cam);

        float fx = cam.at.x - cam.eye.x;
        float fy = cam.at.y - cam.eye.y;
        float fz = cam.at.z - cam.eye.z;
        float fl = sqrtf(fx*fx + fy*fy + fz*fz);
        if (fl > 1e-4f) { fx /= fl; fy /= fl; fz /= fl; }

        /* right = normalize(cross(forward, up(0,1,0))) = normalize(-fz, 0, fx) */
        float rx = -fz, rz = fx;
        float rl = sqrtf(rx*rx + rz*rz);
        if (rl > 1e-4f) { rx /= rl; rz /= rl; }

        int moved = 0;
        if (Fl::get_key('w')) { camX += fx*speed; camY += fy*speed; camZ += fz*speed; moved = 1; }
        if (Fl::get_key('s')) { camX -= fx*speed; camY -= fy*speed; camZ -= fz*speed; moved = 1; }
        if (Fl::get_key('d')) { camX += rx*speed; camZ += rz*speed; moved = 1; }
        if (Fl::get_key('a')) { camX -= rx*speed; camZ -= rz*speed; moved = 1; }
        /* PgUp/PgDn raise/lower (Q/E freed — E is now extrude). */
        if (Fl::get_key(FL_Page_Up))   { camY += speed; moved = 1; }
        if (Fl::get_key(FL_Page_Down)) { camY -= speed; moved = 1; }
        return moved;
    }

    /* Fill a PickCam from the current free-fly camera + viewport, matching the
       70° vertical FOV that draw()'s glSetPerspective uses. */
    void buildPickCam(PickCam *pc)
    {
        Camera cam;
        computeCamera(&cam);
        Vec3 fwd   = editV3Norm(editV3Sub(cam.at, cam.eye));
        Vec3 right = editV3Norm(editV3Cross(fwd, editV3(0.0f, 1.0f, 0.0f)));
        Vec3 up    = editV3Cross(right, fwd);
        pc->eye = cam.eye; pc->forward = fwd; pc->right = right; pc->up = up;
        pc->tanHalfFov = tanf((70.0f * 3.14159265f / 180.0f) * 0.5f);
        int pw = w() > 0 ? w() : 1;
        int ph = h() > 0 ? h() : 1;
        pc->aspect = (float)pw / (float)ph;
        pc->vpW = pw; pc->vpH = ph;
    }

    /* EM0: nearest active entity whose origin projects within radiusPx of the
       cursor (front-most on ties), or -1. Entities are point objects, so this is
       a screen-space origin-distance pick — cf. editPickVertex for the mesh. */
    int pickEntity(const PickCam *pc, float mx, float my, float radiusPx)
    {
        if (!loaded || !scene.entities) return -1;
        int best = -1;
        float bestDepth = 1e30f;
        float r2 = radiusPx * radiusPx;
        int n = scene.entities->count;
        for (int i = 0; i < n; i++) {
            Entity *e = &scene.entities->entities[i];
            if (!e->active) continue;
            Vec3 p = editV3(e->posX, e->posY, e->posZ);
            float sx, sy;
            if (!editProject(pc, p, &sx, &sy)) continue;
            float dx = sx - mx, dy = sy - my;
            if (dx * dx + dy * dy > r2) continue;
            float depth = editDepth(pc, p);
            if (depth < bestDepth) { bestDepth = depth; best = i; }
        }
        return best;
    }

    /* Pick at viewport-local (mx,my) in the active mode. additive (Shift)
       toggles the hit element and keeps the rest; a plain click replaces the
       selection (and clears it on a miss). */
    void doPick(float mx, float my, int additive)
    {
        PickCam pc;
        buildPickCam(&pc);
        if (sel.mode == SEL_VERT) {
            int idx = editPickVertex(&pc, &emesh, mx, my, 8.0f * editDpiScale);
            if (!additive) editSelClearActive(&sel);
            if (idx >= 0) sel.vertSel[idx] = additive ? !sel.vertSel[idx] : 1;
        } else if (sel.mode == SEL_EDGE) {
            int idx = editPickEdge(&pc, &emesh, sel.edges, sel.numEdges, mx, my, 8.0f * editDpiScale);
            if (!additive) editSelClearActive(&sel);
            if (idx >= 0) sel.edgeSel[idx] = additive ? !sel.edgeSel[idx] : 1;
        } else if (sel.mode == SEL_FACE) {
            int idx = editPickFace(&pc, &emesh, mx, my);
            if (!additive) editSelClearActive(&sel);
            if (idx >= 0) sel.faceSel[idx] = additive ? !sel.faceSel[idx] : 1;
        } else { /* SEL_ENTITY */
            int idx = pickEntity(&pc, mx, my, 12.0f * editDpiScale);
            if (!additive) memset(entSel, 0, sizeof(entSel));
            if (idx >= 0) entSel[idx] = additive ? !entSel[idx] : 1;
        }
        refreshPanel();
        redraw();
    }

    /* Set the selection mode and keep the toolbar radio buttons in sync — used
       by both the 1/2/3 keys and the toolbar button callbacks. Setting a radio
       button's value() doesn't clear its siblings, so set all three. */
    void applyMode(EditSelMode m)
    {
        sel.mode = m;
        if (bVert) bVert->value(m == SEL_VERT);
        if (bEdge) bEdge->value(m == SEL_EDGE);
        if (bFace) bFace->value(m == SEL_FACE);
        if (bEnt)  bEnt->value(m == SEL_ENTITY);
        refreshPanel();
        redraw();
    }

    /* ---- M2: modal grab (move) ------------------------------------------ */

    Vec3 grabAxisVec()
    {
        return editV3(grabAxis == 0 ? 1.0f : 0.0f,
                      grabAxis == 1 ? 1.0f : 0.0f,
                      grabAxis == 2 ? 1.0f : 0.0f);
    }

    /* Collect the unique verts the current selection moves, with their pre-grab
       positions + centroid. Returns the count (0 = nothing to grab). */
    int grabGather()
    {
        int nV = emesh.numVerts;
        unsigned char *mark = (unsigned char *)calloc(nV > 0 ? nV : 1, 1);
        int i, j;
        if (sel.mode == SEL_VERT) {
            for (i = 0; i < nV; i++) if (sel.vertSel[i]) mark[i] = 1;
        } else if (sel.mode == SEL_EDGE) {
            for (i = 0; i < sel.numEdges; i++)
                if (sel.edgeSel[i]) { mark[sel.edges[i].a] = 1; mark[sel.edges[i].b] = 1; }
        } else {
            for (i = 0; i < emesh.numFaces; i++)
                if (sel.faceSel[i])
                    for (j = 0; j < emesh.faces[i].nv; j++) mark[emesh.faces[i].v[j]] = 1;
        }
        int n = 0;
        for (i = 0; i < nV; i++) if (mark[i]) n++;
        if (n == 0) { free(mark); return 0; }

        grabVerts = (int *)malloc(n * sizeof(int));
        grabOrig  = (Vec3 *)malloc(n * sizeof(Vec3));
        nGrab = 0;
        Vec3 c = editV3(0, 0, 0);
        for (i = 0; i < nV; i++) {
            if (!mark[i]) continue;
            grabVerts[nGrab] = i;
            grabOrig[nGrab]  = emesh.verts[i].pos;
            c = editV3Add(c, emesh.verts[i].pos);
            nGrab++;
        }
        grabCentroid = editV3Scale(c, 1.0f / (float)nGrab);
        free(mark);
        return nGrab;
    }

    void grabEnd()
    {
        free(grabVerts); free(grabOrig); free(grabOrigYaw);
        grabVerts = NULL; grabOrig = NULL; grabOrigYaw = NULL;
        nGrab = 0; grabbing = 0; rotating = 0;
        grabAxis = -1; grabFromExtrude = 0; grabIsEnt = 0;
    }

    /* EM1: collect the selected entities' slot indices + pre-grab origins +
       centroid into the shared grab arrays (grabVerts holds entity indices).
       Returns the count (0 = nothing to grab). */
    int grabGatherEnts()
    {
        if (!loaded || !scene.entities) return 0;
        int nE = scene.entities->count, i, n = 0;
        for (i = 0; i < nE; i++)
            if (entSel[i] && scene.entities->entities[i].active) n++;
        if (n == 0) return 0;

        grabVerts   = (int *)malloc(n * sizeof(int));
        grabOrig    = (Vec3 *)malloc(n * sizeof(Vec3));
        grabOrigYaw = (float *)malloc(n * sizeof(float));   /* EM2: rotate uses this */
        nGrab = 0;
        Vec3 c = editV3(0, 0, 0);
        for (i = 0; i < nE; i++) {
            if (!entSel[i] || !scene.entities->entities[i].active) continue;
            Entity *e = &scene.entities->entities[i];
            grabVerts[nGrab]   = i;
            grabOrig[nGrab]    = editV3(e->posX, e->posY, e->posZ);
            grabOrigYaw[nGrab] = e->rotY;
            c = editV3Add(c, grabOrig[nGrab]);
            nGrab++;
        }
        grabCentroid = editV3Scale(c, 1.0f / (float)nGrab);
        return nGrab;
    }

    /* Anchor to the mouse and arm the modal move. The caller must have already
       gathered the verts (grabGather) and pushed any undo snapshot. */
    void grabBeginCommon()
    {
        grabAxis = -1;
        grabAnchorX = grabCurX = Fl::event_x();
        grabAnchorY = grabCurY = Fl::event_y();
        grabbing = 1;
        updateMenuEnabled();
    }

    void grabStart()
    {
        if (grabbing) return;
        if (sel.mode == SEL_ENTITY) {
            if (grabGatherEnts() == 0) { conLogf("grab: no entity selected\n"); return; }
            docPushEnts(&hist, scene.entities);  /* pre-grab restore point */
            grabIsEnt = 1; grabFromExtrude = 0;
            grabBeginCommon();
            conLogf("grab: %d entity(ies)\n", nGrab);
            return;
        }
        if (!haveEmesh) return;
        if (grabGather() == 0) { conLogf("grab: nothing selected\n"); return; }
        docPushMesh(&hist, &emesh);              /* pre-grab restore point */
        grabIsEnt = 0; grabFromExtrude = 0;
        grabBeginCommon();
        conLogf("grab: %d vert(s)\n", nGrab);
    }

    /* Recompute moved positions from originals + the current mouse delta. The
       delta lives in the view plane through the selection centroid (screen
       pixels -> world units at that depth); an axis lock projects onto X/Y/Z. */
    void grabApply()
    {
        PickCam pc; buildPickCam(&pc);
        float d = editDepth(&pc, grabCentroid);
        if (d < 0.05f) d = 0.05f;
        float wpp = (2.0f * d * pc.tanHalfFov) / (float)pc.vpH;
        float dxp = (float)(grabCurX - grabAnchorX);
        float dyp = (float)(grabCurY - grabAnchorY);
        Vec3 delta = editV3Add(editV3Scale(pc.right, dxp * wpp),
                               editV3Scale(pc.up,   -dyp * wpp));
        if (grabAxis >= 0) {
            Vec3 ax = grabAxisVec();
            delta = editV3Scale(ax, editV3Dot(delta, ax));
        }
        if (grabIsEnt) {                          /* EM1: move entity origins */
            for (int k = 0; k < nGrab; k++) {
                Vec3 np = editV3Add(grabOrig[k], delta);
                Entity *e = &scene.entities->entities[grabVerts[k]];
                e->posX = editSnap(np.x);
                e->posY = editSnap(np.y);
                e->posZ = editSnap(np.z);
            }
            refreshPanel();
            redraw();
            return;
        }
        for (int k = 0; k < nGrab; k++) {
            Vec3 np = editV3Add(grabOrig[k], delta);
            emesh.verts[grabVerts[k]].pos.x = editSnap(np.x);
            emesh.verts[grabVerts[k]].pos.y = editSnap(np.y);
            emesh.verts[grabVerts[k]].pos.z = editSnap(np.z);
        }
        editMeshBuild(&emesh, &eobj);
        redraw();
    }

    void grabUpdate(int mx, int my) { grabCurX = mx; grabCurY = my; grabApply(); }

    void grabConfirm() { grabEnd(); refreshPanel(); redraw(); }   /* snapshot already on stack */

    void grabCancel()
    {
        if (grabFromExtrude) {
            /* undo the whole extrude+move as one step (removes the new geometry) */
            docPopRestoreMesh(&hist, &emesh);
            grabEnd();
            afterTopologyEdit();
            return;
        }
        if (grabIsEnt) {                          /* EM1: restore entity origins */
            for (int k = 0; k < nGrab; k++) {
                Entity *e = &scene.entities->entities[grabVerts[k]];
                e->posX = grabOrig[k].x; e->posY = grabOrig[k].y; e->posZ = grabOrig[k].z;
            }
            docDropUndoTop(&hist);                /* back to pre-grab: drop it */
            grabEnd();
            updateMenuEnabled();
            refreshPanel();
            redraw();
            return;
        }
        for (int k = 0; k < nGrab; k++)
            emesh.verts[grabVerts[k]].pos = grabOrig[k];
        editMeshBuild(&emesh, &eobj);
        docDropUndoTop(&hist);                    /* back to pre-grab: drop it */
        grabEnd();
        updateMenuEnabled();
        refreshPanel();
        redraw();
    }

    /* ---- EM2: modal yaw-rotate (entities only) --------------------------- */

    void rotStart()
    {
        if (grabbing || rotating) return;
        if (sel.mode != SEL_ENTITY) return;           /* no mesh rotate */
        if (grabGatherEnts() == 0) { conLogf("rotate: no entity selected\n"); return; }
        docPushEnts(&hist, scene.entities);            /* pre-rotate restore point */
        rotating = 1;
        grabAnchorX = grabCurX = Fl::event_x();
        grabAnchorY = grabCurY = Fl::event_y();
        updateMenuEnabled();
        conLogf("rotate: %d entity(ies)\n", nGrab);
    }

    /* Yaw = snapped angle from horizontal mouse travel (15 deg steps, 1 deg with
       Shift). Each entity spins about the shared centroid in XZ — in place when
       only one is selected — and its rotY advances by the same angle, so a
       multi-selection rotates rigidly (orbit + facing match glRotatef about Y). */
    void rotApply()
    {
        const float DEG_PER_PX = 0.5f;
        float step = (Fl::event_state() & FL_SHIFT) ? 1.0f : 15.0f;
        float raw  = (float)(grabCurX - grabAnchorX) * DEG_PER_PX;
        float ang  = step * floorf(raw / step + 0.5f);         /* snap to step */
        float rad  = ang * 3.14159265f / 180.0f;
        float c = cosf(rad), s = sinf(rad);
        for (int k = 0; k < nGrab; k++) {
            Entity *e = &scene.entities->entities[grabVerts[k]];
            float dx = grabOrig[k].x - grabCentroid.x;
            float dz = grabOrig[k].z - grabCentroid.z;
            e->posX = editSnap(grabCentroid.x + dx * c + dz * s);
            e->posZ = editSnap(grabCentroid.z - dx * s + dz * c);
            e->posY = grabOrig[k].y;
            e->rotY = grabOrigYaw[k] + ang;
        }
        refreshPanel();
        redraw();
    }

    void rotUpdate(int mx, int my) { grabCurX = mx; grabCurY = my; rotApply(); }
    void rotConfirm() { grabEnd(); refreshPanel(); redraw(); }   /* snapshot on stack */

    void rotCancel()
    {
        for (int k = 0; k < nGrab; k++) {
            Entity *e = &scene.entities->entities[grabVerts[k]];
            e->posX = grabOrig[k].x; e->posY = grabOrig[k].y; e->posZ = grabOrig[k].z;
            e->rotY = grabOrigYaw[k];
        }
        docDropUndoTop(&hist);                          /* back to pre-rotate: drop it */
        grabEnd();
        updateMenuEnabled();
        refreshPanel();
        redraw();
    }

    /* After an undo/redo restores the mesh: rebuild the render mesh, and only
       re-init the selection if the vert/face counts changed (topology edit);
       for a grab (counts stable) the selection stays valid. */
    void afterRestore()
    {
        editMeshBuild(&emesh, &eobj);
        if (sel.numVerts != emesh.numVerts || sel.numFaces != emesh.numFaces) {
            EditSelMode prev = sel.mode;
            editSelFree(&sel);
            editSelInit(&sel, &emesh);
            applyMode(prev);
        }
        rebuildMatChoice();          /* materials may differ after undo of an open */
        refreshPanel();
        redraw();
    }

    /* Grey out Edit/Undo or Edit/Redo when its stack is empty. */
    void updateMenuEnabled()
    {
        if (!menuBar || undoIdx < 0 || redoIdx < 0) return;
        Fl_Menu_Item *m = (Fl_Menu_Item *)menuBar->menu();
        if (docHasUndo(&hist)) m[undoIdx].activate(); else m[undoIdx].deactivate();
        if (docHasRedo(&hist)) m[redoIdx].activate(); else m[redoIdx].deactivate();
    }

    /* Reconcile whichever document a unified undo/redo just restored: mesh needs
       a render rebuild + selection check; entities just need a repaint. */
    void afterDocRestore(int kind)
    {
        if (kind == DOC_MESH)      afterRestore();
        else if (kind == DOC_ENTS) { refreshPanel(); redraw(); }
    }

    /* Undo/redo entry points shared by the Ctrl+Z/Y keys and the Edit menu.
       Disabled mid-grab (finish or cancel the grab first). One stack spans mesh
       and entity edits, so Ctrl+Z always reverts the most recent of either. */
    void doUndo() { if (!grabbing && !rotating) afterDocRestore(docUndo(&hist, &emesh, scene.entities)); updateMenuEnabled(); }
    void doRedo() { if (!grabbing && !rotating) afterDocRestore(docRedo(&hist, &emesh, scene.entities)); updateMenuEnabled(); }

    /* ---- M3: make-face / flip / delete / add-primitive ------------------ */

    /* World point ~5 m in front of the camera (snapped) — where new primitives
       are dropped. */
    Vec3 focusPoint()
    {
        Camera cam; computeCamera(&cam);
        Vec3 fwd = editV3Norm(editV3Sub(cam.at, cam.eye));
        Vec3 p = editV3Add(cam.eye, editV3Scale(fwd, 5.0f));
        return editV3(editSnap(p.x), editSnap(p.y), editSnap(p.z));
    }

    /* Rebuild the render mesh, reset the selection (topology changed), and
       refresh the Undo/Redo menu state. */
    void afterTopologyEdit()
    {
        editMeshBuild(&emesh, &eobj);
        EditSelMode prev = sel.mode;
        editSelFree(&sel);
        editSelInit(&sel, &emesh);
        applyMode(prev);
        updateMenuEnabled();
        refreshPanel();
        redraw();
    }

    /* F: make a face from 3 or 4 selected verts, ordered around their centroid
       on the best-fit plane so a quad doesn't come out as a bowtie. */
    void makeFace()
    {
        if (grabbing || !haveEmesh) return;
        int idx[4], n = 0, total = 0, i;
        for (i = 0; i < emesh.numVerts; i++)
            if (sel.vertSel[i]) { if (n < 4) idx[n++] = i; total++; }
        if (total < 3 || total > 4) { conLogf("make face: select 3 or 4 vertices\n"); return; }

        Vec3 c = editV3(0, 0, 0);
        for (i = 0; i < total; i++) c = editV3Add(c, emesh.verts[idx[i]].pos);
        c = editV3Scale(c, 1.0f / (float)total);

        Vec3 nrm = editV3(0, 0, 0);                 /* Newell normal */
        for (i = 0; i < total; i++) {
            Vec3 a = emesh.verts[idx[i]].pos;
            Vec3 b = emesh.verts[idx[(i + 1) % total]].pos;
            nrm.x += (a.y - b.y) * (a.z + b.z);
            nrm.y += (a.z - b.z) * (a.x + b.x);
            nrm.z += (a.x - b.x) * (a.y + b.y);
        }
        nrm = editV3Norm(nrm);
        Vec3 ref = (fabsf(nrm.y) < 0.9f) ? editV3(0, 1, 0) : editV3(1, 0, 0);
        Vec3 u = editV3Norm(editV3Cross(ref, nrm));
        Vec3 v = editV3Cross(nrm, u);
        float ang[4];
        for (i = 0; i < total; i++) {
            Vec3 d = editV3Sub(emesh.verts[idx[i]].pos, c);
            ang[i] = atan2f(editV3Dot(d, v), editV3Dot(d, u));
        }
        for (int a = 0; a < total - 1; a++)         /* bubble sort by angle */
            for (int b = 0; b < total - 1 - a; b++)
                if (ang[b] > ang[b + 1]) {
                    float t = ang[b]; ang[b] = ang[b + 1]; ang[b + 1] = t;
                    int ti = idx[b]; idx[b] = idx[b + 1]; idx[b + 1] = ti;
                }

        docPushMesh(&hist, &emesh);
        editAddFace(&emesh, idx[0], idx[1], idx[2], total == 4 ? idx[3] : -1,
                    emesh.numMats > 0 ? 0 : -1);
        afterTopologyEdit();
        conLogf("make face: %d verts\n", total);
    }

    /* Flip the winding/normal of the selected faces (topology unchanged, so the
       selection survives). */
    void flipSelected()
    {
        if (grabbing || !haveEmesh) return;
        int n = 0, i;
        for (i = 0; i < emesh.numFaces; i++) if (sel.faceSel[i]) n++;
        if (n == 0) { conLogf("flip: select faces (mode 3) first\n"); return; }
        docPushMesh(&hist, &emesh);
        for (i = 0; i < emesh.numFaces; i++) if (sel.faceSel[i]) editFlipFace(&emesh, i);
        editMeshBuild(&emesh, &eobj);
        updateMenuEnabled();
        redraw();
        conLogf("flip: %d face(s)\n", n);
    }

    /* Delete the selection: in vertex mode the selected verts and every face
       using them; in face/edge mode the selected faces (or faces on a selected
       edge) plus any verts orphaned by the removal. */
    void deleteSelected()
    {
        if (grabbing || !haveEmesh) return;
        int nv = emesh.numVerts, nf = emesh.numFaces, i, j;
        unsigned char *keepV = (unsigned char *)malloc(nv > 0 ? nv : 1);
        unsigned char *keepF = (unsigned char *)malloc(nf > 0 ? nf : 1);
        for (i = 0; i < nv; i++) keepV[i] = 1;
        for (i = 0; i < nf; i++) keepF[i] = 1;
        int any = 0;

        if (sel.mode == SEL_VERT) {
            for (i = 0; i < nv; i++) if (sel.vertSel[i]) { keepV[i] = 0; any = 1; }
            for (i = 0; i < nf; i++)
                for (j = 0; j < emesh.faces[i].nv; j++)
                    if (!keepV[emesh.faces[i].v[j]]) { keepF[i] = 0; break; }
        } else if (sel.mode == SEL_FACE) {
            for (i = 0; i < nf; i++) if (sel.faceSel[i]) { keepF[i] = 0; any = 1; }
        } else { /* SEL_EDGE: drop faces that contain a whole selected edge */
            for (i = 0; i < nf; i++) {
                EditFace *f = &emesh.faces[i];
                for (int e = 0; e < sel.numEdges && keepF[i]; e++) {
                    if (!sel.edgeSel[e]) continue;
                    int a = sel.edges[e].a, b = sel.edges[e].b, hasA = 0, hasB = 0;
                    for (j = 0; j < f->nv; j++) { if (f->v[j] == a) hasA = 1; if (f->v[j] == b) hasB = 1; }
                    if (hasA && hasB) { keepF[i] = 0; any = 1; }
                }
            }
        }
        if (!any) { free(keepV); free(keepF); conLogf("delete: nothing selected\n"); return; }

        if (sel.mode != SEL_VERT) {                 /* drop verts no face uses */
            for (i = 0; i < nv; i++) keepV[i] = 0;
            for (i = 0; i < nf; i++)
                if (keepF[i]) for (j = 0; j < emesh.faces[i].nv; j++)
                    keepV[emesh.faces[i].v[j]] = 1;
        }

        docPushMesh(&hist, &emesh);
        editMeshCompact(&emesh, keepV, keepF);
        free(keepV); free(keepF);
        afterTopologyEdit();
        conLogf("delete: -> %d verts, %d faces\n", emesh.numVerts, emesh.numFaces);
    }

    /* N: recompute consistent, outward-facing normals (Blender Shift+N). Winding
       only, so the selection survives — no afterTopologyEdit. */
    void recalcNormals()
    {
        if (grabbing || !haveEmesh) return;
        docPushMesh(&hist, &emesh);
        int flips = editRecalcNormalsConsistent(&emesh);
        if (flips == 0) { docDropUndoTop(&hist); conLogf("recalc normals: already consistent\n"); return; }
        editMeshBuild(&emesh, &eobj);
        updateMenuEnabled();
        redraw();
        conLogf("recalc normals: %d face(s) flipped\n", flips);
    }

    /* M: weld coincident verts (merge-by-distance, half a snap cell). Topology
       changes, so reset the selection via afterTopologyEdit. */
    void mergeVerts()
    {
        if (grabbing || !haveEmesh) return;
        docPushMesh(&hist, &emesh);
        int removed = editMergeByDistance(&emesh, EDIT_SNAP * 0.5f);
        if (removed == 0) { docDropUndoTop(&hist); conLogf("merge: nothing to weld\n"); return; }
        afterTopologyEdit();
        conLogf("merge: welded %d vert(s)\n", removed);
    }

    void addCube()
    {
        if (grabbing || !haveEmesh) return;
        docPushMesh(&hist, &emesh);
        Vec3 c = focusPoint();
        editAddCube(&emesh, c.x, c.y, c.z, 1.0f, 1.0f, 1.0f, emesh.numMats > 0 ? 0 : -1);
        afterTopologyEdit();
        conLogf("add cube at %.2f %.2f %.2f\n", c.x, c.y, c.z);
    }

    void addPlane()
    {
        if (grabbing || !haveEmesh) return;
        docPushMesh(&hist, &emesh);
        Vec3 c = focusPoint();
        float h = 1.0f;
        int matId = emesh.numMats > 0 ? 0 : -1;
        editAddQuad(&emesh, editV3(c.x - h, c.y, c.z - h), editV3(c.x - h, c.y, c.z + h),
                            editV3(c.x + h, c.y, c.z + h), editV3(c.x + h, c.y, c.z - h), matId);
        afterTopologyEdit();
        conLogf("add plane at %.2f %.2f %.2f\n", c.x, c.y, c.z);
    }

    /* E: extrude the selected faces, then auto-grab the new cap so you drag it
       out to height (locked to the face's axis when it's axis-aligned). One
       undo step covers extrude+move; Esc removes the whole extrusion. */
    void extrudeSelection()
    {
        if (grabbing || !haveEmesh) return;
        int nf = emesh.numFaces, i, nsel = 0;
        for (i = 0; i < nf; i++) if (sel.faceSel[i]) nsel++;
        if (nsel == 0) { conLogf("extrude: select faces (mode 3) first\n"); return; }

        docPushMesh(&hist, &emesh);          /* one snapshot for extrude+move */

        unsigned char *cap = (unsigned char *)malloc(nf);
        for (i = 0; i < nf; i++) cap[i] = sel.faceSel[i];

        editExtrude(&emesh, cap);                /* cap faces keep their indices */

        Vec3 avg = editV3(0, 0, 0);              /* dominant axis of the cap */
        for (i = 0; i < nf; i++) if (cap[i]) avg = editV3Add(avg, emesh.faces[i].normal);
        avg = editV3Norm(avg);

        editMeshBuild(&emesh, &eobj);
        editSelFree(&sel);
        editSelInit(&sel, &emesh);
        for (i = 0; i < nf; i++) if (cap[i]) sel.faceSel[i] = 1;   /* reselect cap */
        applyMode(SEL_FACE);
        free(cap);

        grabFromExtrude = 1;
        if (grabGather() > 0) {
            grabBeginCommon();
            if      (fabsf(avg.x) > 0.9f) grabAxis = 0;   /* lock to the face axis */
            else if (fabsf(avg.y) > 0.9f) grabAxis = 1;
            else if (fabsf(avg.z) > 0.9f) grabAxis = 2;
            else                          grabAxis = -1;
        } else {
            grabFromExtrude = 0;
        }
        refreshPanel();
        redraw();
        conLogf("extrude: %d face(s)\n", nsel);
    }

    /* ---- M5: material / tiling property panel --------------------------- */

    /* The material the panel edits: the Fl_Choice item index == material index
       (they're listed in order). */
    int panelMatId()
    {
        if (!matChoice) return -1;
        int v = matChoice->value();
        return (v >= 0 && v < emesh.numMats) ? v : -1;
    }

    void rebuildMatChoice()
    {
        if (!matChoice) return;
        int keep = matChoice->value();
        matChoice->clear();
        for (int i = 0; i < emesh.numMats; i++) {
            char nm[64];                         /* '/' and '&' are special in menus */
            const char *s = emesh.mats[i].name;
            int j = 0;
            for (; s[j] && j < 63; j++)
                nm[j] = (s[j] == '/' || s[j] == '&' || s[j] == '\\') ? '_' : s[j];
            nm[j] = 0;
            if (nm[0] == 0) { nm[0] = 'm'; nm[1] = 0; }
            matChoice->add(nm);
        }
        if (keep >= 0 && keep < emesh.numMats) matChoice->value(keep);
        else if (emesh.numMats > 0)            matChoice->value(0);
    }

    /* Show the selected faces' material in the face-props widgets. */
    void refreshFacePanel()
    {
        int id = -1, i;
        for (i = 0; i < emesh.numFaces; i++)
            if (sel.faceSel[i]) { id = emesh.faces[i].materialId; break; }
        if (id < 0 || id >= emesh.numMats) id = panelMatId();
        if (id < 0 && emesh.numMats > 0) id = 0;
        if (id < 0) return;
        matChoice->value(id);
        scaleInput->value(emesh.mats[id].tilingScale);
        offXInput->value(emesh.mats[id].tilingOffsetX);
        offYInput->value(emesh.mats[id].tilingOffsetY);
        diffuseInput->value(emesh.mats[id].diffusePath);
    }

    void refreshVertPanel(int vi)
    {
        panelVert = vi;
        vertEditPushed = 0;              /* next edit starts a new undo gesture */
        vxInput->value(emesh.verts[vi].pos.x);
        vyInput->value(emesh.verts[vi].pos.y);
        vzInput->value(emesh.verts[vi].pos.z);
    }

    /* Pick the sub-panel to show from mode + selection:
       vertex mode with exactly one vert -> X/Y/Z position;
       face mode with any faces selected -> material/tiling;
       otherwise (incl. edge mode)       -> empty. */
    void refreshPanel()
    {
        if (!faceGroup || !vertGroup) return;
        if (entGroup && sel.mode != SEL_ENTITY) entGroup->hide();
        if (sel.mode == SEL_FACE) {
            int n = 0;
            for (int i = 0; i < emesh.numFaces; i++) if (sel.faceSel[i]) n++;
            vertGroup->hide();
            if (n > 0) { faceGroup->show(); refreshFacePanel(); }
            else         faceGroup->hide();
        } else if (sel.mode == SEL_VERT) {
            int one = -1, n = 0;
            for (int i = 0; i < emesh.numVerts; i++) if (sel.vertSel[i]) { one = i; n++; }
            faceGroup->hide();
            if (n == 1) { vertGroup->show(); refreshVertPanel(one); }
            else        { vertGroup->hide(); panelVert = -1; }
        } else if (sel.mode == SEL_ENTITY) {
            /* EM3: show the entity form for exactly one selected entity. */
            faceGroup->hide(); vertGroup->hide(); panelVert = -1;
            int one = -1, n = 0;
            if (loaded && scene.entities)
                for (int i = 0; i < scene.entities->count; i++)
                    if (entSel[i] && scene.entities->entities[i].active) { one = i; n++; }
            if (entGroup) {
                if (n == 1) { entGroup->show(); refreshEntPanel(one); }
                else        { entGroup->hide(); panelEnt = -1; }
            }
        } else {  /* SEL_EDGE */
            faceGroup->hide(); vertGroup->hide(); panelVert = -1;
        }
        if (propPanel) propPanel->redraw();
    }

    /* Edit the shown vertex's position from the X/Y/Z fields (snapped, undoable). */
    void onVertPosChanged()
    {
        if (panelVert < 0 || panelVert >= emesh.numVerts) return;
        /* Push undo once per gesture, so a live slide doesn't spam the stack. */
        if (!vertEditPushed) { docPushMesh(&hist, &emesh); vertEditPushed = 1; updateMenuEnabled(); }
        emesh.verts[panelVert].pos.x = editSnap((float)vxInput->value());
        emesh.verts[panelVert].pos.y = editSnap((float)vyInput->value());
        emesh.verts[panelVert].pos.z = editSnap((float)vzInput->value());
        editMeshBuild(&emesh, &eobj);
        updateMenuEnabled();
        redraw();
    }

    /* ---- EM3: entity property form ------------------------------------- */

    static const char *entTypeName(int t)
    {
        switch (t) {
        case ENT_PLAYER:     return "player";
        case ENT_DECORATION: return "decoration";
        case ENT_ITEM:       return "item";
        case ENT_ENEMY:      return "enemy";
        case ENT_PLATFORM:   return "platform";
        case ENT_SWITCH:     return "switch";
        case ENT_TRIGGER:    return "trigger";
        case ENT_DOOR:       return "door";
        case ENT_WAYPOINT:   return "waypoint";
        case ENT_PATH_NODE:  return "path_node";
        case ENT_LIGHT:      return "light";
        default:             return "none";
        }
    }

    void hideTypeGroups()
    {
        if (entLightGroup) entLightGroup->hide();
        if (gItem) gItem->hide();         if (gEnemy) gEnemy->hide();
        if (gPlatform) gPlatform->hide(); if (gSwitch) gSwitch->hide();
        if (gTrigger) gTrigger->hide();   if (gDoor) gDoor->hide();
        if (gPath) gPath->hide();
    }

    /* Load the selected entity's fields into the form and reveal the sub-group
       matching its type. Resets the one-undo-per-gesture guard. */
    void refreshEntPanel(int ei)
    {
        panelEnt = ei;
        entEditPushed = 0;
        Entity *e = &scene.entities->entities[ei];
        entTypeBox->copy_label(entTypeName(e->type));
        entNameIn->value(e->name);
        entGroupIn->value(e->group);
        entXIn->value(e->posX); entYIn->value(e->posY); entZIn->value(e->posZ);
        entRotIn->value(e->rotY); entScaleIn->value(e->scale);
        hideTypeGroups();
        switch (e->type) {
        case ENT_LIGHT:
            entLrIn->value(e->light.r); entLgIn->value(e->light.g); entLbIn->value(e->light.b);
            entLiIn->value(e->light.intensity); entLradIn->value(e->light.radius);
            entLightGroup->show(); break;
        case ENT_ITEM:
            itType->value(e->item.itemType); gItem->show(); break;
        case ENT_ENEMY:
            enHealth->value(e->enemy.health); enSpeed->value(e->enemy.speed);
            enSight->value(e->enemy.sightRange); gEnemy->show(); break;
        case ENT_PLATFORM:
            plPath->value(e->platform.pathGroup); plMove->value(e->platform.moveType);
            plSpeed->value(e->platform.speed); plEnabled->value(e->platform.enabled);
            plFace->value(e->platform.facePath); gPlatform->show(); break;
        case ENT_SWITCH:
            swTarget->value(e->sw.target); gSwitch->show(); break;
        case ENT_TRIGGER:
            trSx->value(e->trigger.sizeX); trSy->value(e->trigger.sizeY);
            trSz->value(e->trigger.sizeZ); trTarget->value(e->trigger.target);
            trOnce->value(e->trigger.once); gTrigger->show(); break;
        case ENT_DOOR:
            drMotion->value(e->door.motion); drAxis->value(e->door.axis);
            drAmount->value(e->door.amount); drSpeed->value(e->door.speed);
            drAuto->value(e->door.autoCloseTime); gDoor->show(); break;
        case ENT_PATH_NODE:
            paOrder->value(e->pathNode.order); gPath->show(); break;
        default: break;
        }
    }

    /* Write the form back into the entity — snapped position, one undo push per
       edit gesture. Light fields apply only to ENT_LIGHT. */
    void onEntChanged()
    {
        if (!loaded || !scene.entities) return;
        if (panelEnt < 0 || panelEnt >= scene.entities->count) return;
        Entity *e = &scene.entities->entities[panelEnt];
        if (!e->active) return;
        if (!entEditPushed) { docPushEnts(&hist, scene.entities); entEditPushed = 1; updateMenuEnabled(); }
        strncpy(e->name,  entNameIn->value(),  31); e->name[31]  = '\0';
        strncpy(e->group, entGroupIn->value(), 31); e->group[31] = '\0';
        e->posX  = editSnap((float)entXIn->value());
        e->posY  = editSnap((float)entYIn->value());
        e->posZ  = editSnap((float)entZIn->value());
        e->rotY  = (float)entRotIn->value();
        e->scale = (float)entScaleIn->value();
        switch (e->type) {
        case ENT_LIGHT:
            e->light.r = (float)entLrIn->value();
            e->light.g = (float)entLgIn->value();
            e->light.b = (float)entLbIn->value();
            e->light.intensity = (float)entLiIn->value();
            e->light.radius    = (float)entLradIn->value();
            break;
        case ENT_ITEM:
            e->item.itemType = itType->value();
            break;
        case ENT_ENEMY:
            e->enemy.health     = (int)enHealth->value();
            e->enemy.speed      = (float)enSpeed->value();
            e->enemy.sightRange = (float)enSight->value();
            break;
        case ENT_PLATFORM:
            strncpy(e->platform.pathGroup, plPath->value(), 31); e->platform.pathGroup[31] = '\0';
            e->platform.moveType = plMove->value();
            e->platform.speed    = (float)plSpeed->value();
            e->platform.enabled  = plEnabled->value();
            e->platform.facePath = plFace->value();
            break;
        case ENT_SWITCH:
            strncpy(e->sw.target, swTarget->value(), 31); e->sw.target[31] = '\0';
            break;
        case ENT_TRIGGER:
            e->trigger.sizeX = (float)trSx->value();
            e->trigger.sizeY = (float)trSy->value();
            e->trigger.sizeZ = (float)trSz->value();
            strncpy(e->trigger.target, trTarget->value(), 31); e->trigger.target[31] = '\0';
            e->trigger.once = trOnce->value();
            break;
        case ENT_DOOR:
            e->door.motion        = drMotion->value();
            e->door.axis          = drAxis->value();
            e->door.amount        = (float)drAmount->value();
            e->door.speed         = (float)drSpeed->value();
            e->door.autoCloseTime = (float)drAuto->value();
            break;
        case ENT_PATH_NODE:
            e->pathNode.order = (int)paOrder->value();
            break;
        default: break;
        }
        updateMenuEnabled();
        redraw();
    }

    /* Live-edit the current material's tiling (shared by every face using it). */
    void onTilingChanged()
    {
        int id = panelMatId();
        if (id < 0) return;
        float s = (float)scaleInput->value();
        emesh.mats[id].tilingScale   = (s > 0.001f) ? s : 1.0f;
        emesh.mats[id].tilingOffsetX = (float)offXInput->value();
        emesh.mats[id].tilingOffsetY = (float)offYInput->value();
        editMeshBuild(&emesh, &eobj);
        redraw();
    }

    void onDiffuseChanged()
    {
        int id = panelMatId();
        if (id < 0) return;
        strncpy(emesh.mats[id].diffusePath, diffuseInput->value(), 127);
        emesh.mats[id].diffusePath[127] = 0;
        editMeshBuild(&emesh, &eobj);            /* renderer reloads by path */
        redraw();
    }

    /* Assign the chosen material to the selected faces. */
    void onMaterialChosen()
    {
        int id = matChoice->value();
        if (id < 0 || id >= emesh.numMats) return;
        for (int i = 0; i < emesh.numFaces; i++)
            if (sel.faceSel[i]) emesh.faces[i].materialId = id;
        editMeshBuild(&emesh, &eobj);
        refreshPanel();
        redraw();
    }

    void onAddMaterial()
    {
        if (emesh.numMats >= OBJ_MAX_MATERIALS) { conLogf("materials full\n"); return; }
        int id = emesh.numMats++;
        int base = panelMatId();
        if (base >= 0) emesh.mats[id] = emesh.mats[base];   /* clone current */
        else { memset(&emesh.mats[id], 0, sizeof(Material)); emesh.mats[id].tilingScale = 1.0f; }
        snprintf(emesh.mats[id].name, 64, "material_%d", id);
        rebuildMatChoice();
        matChoice->value(id);
        onMaterialChosen();                      /* assign to selection + refresh */
        conLogf("added material_%d\n", id);
    }

    /* ---- M6: save / load / export --------------------------------------- */

    void doSave()
    {
        const char *p = fl_file_chooser("Save Level (.lvl)", "*.lvl", "level.lvl");
        if (p) editSaveLvl(&emesh, p);
    }

    void doOpen()
    {
        const char *p = fl_file_chooser("Open Level (.lvl)", "*.lvl", 0);
        if (!p) return;
        docPushMesh(&hist, &emesh);          /* opening is undoable */
        editMeshFree(&emesh);
        editLoadLvl(&emesh, p);
        editMeshBuild(&emesh, &eobj);
        editSelFree(&sel);
        editSelInit(&sel, &emesh);
        applyMode(SEL_VERT);
        rebuildMatChoice();
        refreshPanel();
        updateMenuEnabled();
        redraw();
    }

    void doExportObj()
    {
        const char *p = fl_file_chooser("Export OBJ (+MTL)", "*.obj", "level.obj");
        if (!p) return;
        char mtl[512];
        strncpy(mtl, p, sizeof(mtl) - 1); mtl[sizeof(mtl) - 1] = 0;
        int n = (int)strlen(mtl);                /* swap .obj -> .mtl, else append */
        if (n > 4 && strcmp(mtl + n - 4, ".obj") == 0) strcpy(mtl + n - 4, ".mtl");
        else strncat(mtl, ".mtl", sizeof(mtl) - strlen(mtl) - 1);
        editExportObj(&emesh, p, mtl);
    }

    int handle(int e)
    {
        switch (e) {
        case FL_PUSH:
            if (grabbing || rotating) {
                /* left = confirm, right = cancel (grab or rotate); swallow the
                   matching release so it doesn't also select. */
                int left  = (Fl::event_button() == FL_LEFT_MOUSE);
                int right = (Fl::event_button() == FL_RIGHT_MOUSE);
                if (grabbing) { if (left) grabConfirm(); else if (right) grabCancel(); }
                else          { if (left) rotConfirm();  else if (right) rotCancel();  }
                suppressRelease = 1;
                return 1;
            }
            lastX = pushX = Fl::event_x();
            lastY = pushY = Fl::event_y();
            take_focus();
            return 1;
        case FL_RELEASE: {
            if (suppressRelease) { suppressRelease = 0; return 1; }
            /* Left click (press+release that barely moved) = select. Right
               button is camera-only, so it never selects. */
            int dx = Fl::event_x() - pushX;
            int dy = Fl::event_y() - pushY;
            if (Fl::event_button() == FL_LEFT_MOUSE &&
                (haveEmesh || sel.mode == SEL_ENTITY) &&
                dx * dx + dy * dy <= 16) {
                /* EditorView is an Fl_Gl_Window (a subwindow), so event coords
                   are already local to the viewport — do NOT subtract x()/y(),
                   or the toolbar height gets double-counted and picks land ~30px
                   too high. */
                int mx = Fl::event_x();
                int my = Fl::event_y();
                int add = (Fl::event_state() & FL_SHIFT) ? 1 : 0;
                doPick((float)mx, (float)my, add);
            }
            return 1;
        }
        case FL_DRAG: {
            int dx = Fl::event_x() - lastX;
            int dy = Fl::event_y() - lastY;
            lastX = Fl::event_x();
            lastY = Fl::event_y();
            /* Right-drag orbits the camera; left-drag is reserved for selection
               (no rotation), so a left click-select never fights the look. */
            if (Fl::event_state() & FL_BUTTON3) {
                yaw   += dx * 0.005f;
                pitch -= dy * 0.005f;
                if (pitch >  1.55f) pitch =  1.55f;
                if (pitch < -1.55f) pitch = -1.55f;
                redraw();
            }
            return 1;
        }
        case FL_MOUSEWHEEL: {
            Camera cam;
            computeCamera(&cam);
            float fx = cam.at.x - cam.eye.x;
            float fy = cam.at.y - cam.eye.y;
            float fz = cam.at.z - cam.eye.z;
            float step = -Fl::event_dy() * 0.6f;
            camX += fx*step; camY += fy*step; camZ += fz*step;
            redraw();
            return 1;
        }
        case FL_ENTER:
            return 1;               /* claim belowmouse so FL_MOVE arrives */
        case FL_MOVE:
            if (grabbing)      grabUpdate(Fl::event_x(), Fl::event_y());
            else if (rotating) rotUpdate(Fl::event_x(), Fl::event_y());
            return 1;
        case FL_FOCUS:
        case FL_UNFOCUS:
            return 1;               /* keep receiving keyboard focus */
        case FL_KEYDOWN: {
            int k  = Fl::event_key();
            int st = Fl::event_state();
            if (grabbing) {
                /* axis locks toggle (press again to release); Enter confirms,
                   Esc cancels. Everything else is swallowed mid-grab. */
                if (k == 'x' || k == 'X') { grabAxis = (grabAxis == 0) ? -1 : 0; grabApply(); return 1; }
                if (k == 'y' || k == 'Y') { grabAxis = (grabAxis == 1) ? -1 : 1; grabApply(); return 1; }
                if (k == 'z' || k == 'Z') { grabAxis = (grabAxis == 2) ? -1 : 2; grabApply(); return 1; }
                if (k == FL_Enter || k == FL_KP_Enter) { grabConfirm(); return 1; }
                if (k == FL_Escape) { grabCancel(); return 1; }
                return 1;
            }
            if (rotating) {
                /* Enter confirms, Esc cancels; everything else swallowed. Shift
                   (fine step) is read live in rotApply on the next mouse move. */
                if (k == FL_Enter || k == FL_KP_Enter) { rotConfirm(); return 1; }
                if (k == FL_Escape) { rotCancel(); return 1; }
                return 1;
            }
            /* 1/2/3/4 select mode; G grab; R rotate (entities); Ctrl+Z/Y undo.
               WASD/QE movement is polled by the frame timer, untouched here. */
            if (k == '1') { applyMode(SEL_VERT); return 1; }
            if (k == '2') { applyMode(SEL_EDGE); return 1; }
            if (k == '3') { applyMode(SEL_FACE); return 1; }
            if (k == '4') { applyMode(SEL_ENTITY); return 1; }
            if (k == 'g' || k == 'G') { grabStart(); return 1; }
            if (k == 'r' || k == 'R') { rotStart(); return 1; }
            /* Mesh ops act on the geometry document, so they're inert in entity
               mode (grabStart above already no-ops there — no verts to gather).
               EM4 will give entity mode its own delete. */
            if (sel.mode != SEL_ENTITY) {
                if (k == 'f' || k == 'F') { makeFace(); return 1; }
                if (k == 'e' || k == 'E') { extrudeSelection(); return 1; }
                if (k == 'n' || k == 'N') { recalcNormals(); return 1; }
                if (k == 'm' || k == 'M') { mergeVerts(); return 1; }
                if (k == 'x' || k == 'X' || k == FL_Delete) { deleteSelected(); return 1; }
            }
            if ((st & FL_CTRL) && (k == 'z' || k == 'Z')) { doUndo(); return 1; }
            if ((st & FL_CTRL) && (k == 'y' || k == 'Y')) { doRedo(); return 1; }
            /* let Ctrl/Cmd combos through to the menu accelerators (Ctrl+S/O) */
            if (st & (FL_CTRL | FL_META)) return 0;
            return 1;
        }
        case FL_KEYUP:
            return 1;               /* movement handled by the frame timer */
        }
        return Fl_Gl_Window::handle(e);
    }
};

/* ~60 Hz frame pump: poll movement keys, redraw when the camera moves. */
static void frameTimer(void *v)
{
    EditorView *view = (EditorView *)v;
    if (view->moveFromKeys())
        view->redraw();
    Fl::repeat_timeout(1.0 / 60.0, frameTimer, v);
}

/* Toolbar select-mode buttons: route through the same applyMode() the 1/2/3
   keys use, so keyboard and toolbar stay in sync. */
static void modeButtonCb(Fl_Widget *w, void *v)
{
    EditorView *view = (EditorView *)v;
    if (w == view->bVert)      view->applyMode(SEL_VERT);
    else if (w == view->bEdge) view->applyMode(SEL_EDGE);
    else if (w == view->bFace) view->applyMode(SEL_FACE);
    else                       view->applyMode(SEL_ENTITY);
}

/* Menu callbacks. Undo/Redo also have Ctrl+Z/Y accelerators; the viewport
   handles those keys when focused, so exactly one undo fires either way. */
static int confirmExit()
{
    /* b0 "Cancel" is the default (Enter/Esc) — safe default for a destructive
       action; returns 1 only when "Exit" is explicitly chosen. */
    return fl_choice("Exit the SOOB editor?", "Cancel", "Exit", 0) == 1;
}
static void menuExitCb(Fl_Widget *, void *v) { if (confirmExit()) ((Fl_Window *)v)->hide(); }
static void winCloseCb(Fl_Widget *w, void *) { if (confirmExit()) w->hide(); }
static void menuSaveCb  (Fl_Widget *, void *v) { ((EditorView *)v)->doSave(); }
static void menuOpenCb  (Fl_Widget *, void *v) { ((EditorView *)v)->doOpen(); }
static void menuExportCb(Fl_Widget *, void *v) { ((EditorView *)v)->doExportObj(); }
static void menuUndoCb(Fl_Widget *, void *v) { ((EditorView *)v)->doUndo(); }
static void menuRedoCb(Fl_Widget *, void *v) { ((EditorView *)v)->doRedo(); }
static void menuExtrudeCb (Fl_Widget *, void *v) { ((EditorView *)v)->extrudeSelection(); }
static void menuAddCubeCb (Fl_Widget *, void *v) { ((EditorView *)v)->addCube(); }
static void menuAddPlaneCb(Fl_Widget *, void *v) { ((EditorView *)v)->addPlane(); }
static void menuMakeFaceCb(Fl_Widget *, void *v) { ((EditorView *)v)->makeFace(); }
static void menuFlipCb    (Fl_Widget *, void *v) { ((EditorView *)v)->flipSelected(); }
static void menuRecalcCb  (Fl_Widget *, void *v) { ((EditorView *)v)->recalcNormals(); }
static void menuMergeCb   (Fl_Widget *, void *v) { ((EditorView *)v)->mergeVerts(); }
static void menuDeleteCb  (Fl_Widget *, void *v) { ((EditorView *)v)->deleteSelected(); }

/* Property-panel callbacks. */
static void matChoiceCb(Fl_Widget *, void *v) { ((EditorView *)v)->onMaterialChosen(); }
static void addMatCb   (Fl_Widget *, void *v) { ((EditorView *)v)->onAddMaterial(); }
static void diffuseCb  (Fl_Widget *, void *v) { ((EditorView *)v)->onDiffuseChanged(); }
static void tilingCb   (Fl_Widget *, void *v) { ((EditorView *)v)->onTilingChanged(); }
static void vertPosCb  (Fl_Widget *, void *v) { ((EditorView *)v)->onVertPosChanged(); }
static void entFieldCb (Fl_Widget *, void *v) { ((EditorView *)v)->onEntChanged(); }

/* Bold, left-aligned section header added to the currently-open group. */
static void addPanelHeader(int x, int y, int w, const char *txt)
{
    Fl_Box *h = new Fl_Box(x, y, w, 18, txt);
    h->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    h->labelfont(FL_HELVETICA_BOLD);
}

/* FLTK reports problems through Fl::error / Fl::warning, which default to
   stderr -- and COMMAND.COM has no 2>&1, so on Win98 those messages are simply
   lost. Route them into conLogf, which flushes, so `SoobEditor > log.txt`
   captures them. Fl::fatal must not return. */
static void editFlMsg(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = '\0';
    conLogf("FLTK: %s\n", buf);
}

static void editFlFatal(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = '\0';
    conLogf("FLTK FATAL: %s\n", buf);
    exit(1);
}

/* Pick the richest GL mode the driver actually supports, rather than assuming
   one. A GeForce 4 MX under Win98 may refuse the combination that works on a
   modern driver, and Fl_Gl_Window::show() responds to an unsupported mode by
   calling Fl::error and returning WITHOUT creating the window
   (Fl_Gl_Window.cxx:77) -- which is how the editor ended up running with no
   window at all. Depth is required for the 3D view; the last two entries exist
   only so the failure is reported rather than silent. */
static int editPickGlMode(Fl_Gl_Window *view)
{
    static const int modes[] = {
        FL_RGB | FL_DEPTH | FL_DOUBLE,
        FL_RGB | FL_DEPTH,
        FL_RGB | FL_DOUBLE,
        FL_RGB
    };
    static const char *names[] = {
        "RGB + DEPTH + DOUBLE",
        "RGB + DEPTH (single-buffered)",
        "RGB + DOUBLE (NO DEPTH - 3D view will be wrong)",
        "RGB (NO DEPTH - 3D view will be wrong)"
    };

    for (int i = 0; i < 4; i++) {
        if (Fl_Gl_Window::can_do(modes[i])) {
            view->mode(modes[i]);
            conLogf("editor: GL mode %s\n", names[i]);
            return 1;
        }
        conLogf("editor: GL mode %s -- not available\n", names[i]);
    }
    return 0;
}

int main(int argc, char **argv)
{
    /* First: DPI awareness must be declared before FLTK opens the display.
       UI font is 12 px at 96 DPI (FLTK default is 14), scaled for the display. */
    editDpiInit(12);

    Fl::error = editFlMsg;
    Fl::warning = editFlMsg;
    Fl::fatal = editFlFatal;

    /* Fl::gl_visual() used to be called here. It configures the visual for
       gl_start()/gl_finish() drawing into a NORMAL window, which this editor
       never does -- Fl_Gl_Window carries its own mode. Worse, it made the
       depth buffer an app-wide assumption that nothing verified. The mode is
       now negotiated per-window in editPickGlMode below. */

    const int W = 1024, H = 768;
    const int MB = 25;                 /* menu-bar height */
    const int TB = 30;                 /* toolbar height */
    const int PW = 220;                /* right property-panel width */
    const int TOP = MB + TB;           /* content top (below menu + toolbar) */

    Fl_Window *win = new Fl_Window(W, H, "SOOB Level Editor");
    win->begin();

    /* Menu bar. Child widget coords are absolute (window space), so the toolbar
       and everything below it are offset by the menu/toolbar heights. */
    Fl_Menu_Bar *menu = new Fl_Menu_Bar(0, 0, W, MB);

    /* Toolbar: select-mode radio buttons, icons from editor/icons/*.xpm.
       FL_RADIO_BUTTON gives one-lit-at-a-time behaviour for free. */
    Fl_Group *toolbar = new Fl_Group(0, MB, W, TB);
    toolbar->box(FL_UP_BOX);
    Fl_Button *bVert = new Fl_Button(4,  MB + 3, 34, TB - 6);
    Fl_Button *bEdge = new Fl_Button(40, MB + 3, 34, TB - 6);
    Fl_Button *bFace = new Fl_Button(76, MB + 3, 34, TB - 6);
    Fl_Button *bEnt  = new Fl_Button(112, MB + 3, 34, TB - 6);
    bVert->image(editDpiImage(new Fl_Pixmap(mode_vert_xpm))); bVert->type(FL_RADIO_BUTTON);
    bEdge->image(editDpiImage(new Fl_Pixmap(mode_edge_xpm))); bEdge->type(FL_RADIO_BUTTON);
    bFace->image(editDpiImage(new Fl_Pixmap(mode_face_xpm))); bFace->type(FL_RADIO_BUTTON);
    bEnt->image(editDpiImage(new Fl_Pixmap(mode_entity_xpm))); bEnt->type(FL_RADIO_BUTTON);
    bVert->tooltip("Vertex select (1)");
    bEdge->tooltip("Edge select (2)");
    bFace->tooltip("Face select (3)");
    bEnt->tooltip("Entity select (4)");
    toolbar->end();
    toolbar->resizable(NULL);          /* buttons stay put when the window resizes */

    /* Right-side property panel. Two sub-groups overlap the area below the
       title; refreshPanel() shows the one matching the mode + selection. */
    Fl_Group *panel = new Fl_Group(W - PW, TOP, PW, H - TOP);
    panel->box(FL_UP_BOX);

    int px = W - PW + 74, pw = PW - 82;

    /* Face props: material + tiling (face mode, faces selected). */
    Fl_Group *faceGroup = new Fl_Group(W - PW, TOP, PW, H - TOP);
    int fy = TOP + 10;
    Fl_Choice      *mc = new Fl_Choice(px, fy, pw, 22, "Material:");     fy += 28;
    Fl_Button      *ab = new Fl_Button(W - PW + 8, fy, PW - 16, 22, "Add Material"); fy += 30;
    Fl_Input       *di = new Fl_Input(px, fy, pw, 22, "Diffuse:");       fy += 30;
    Fl_Value_Input *si = new Fl_Value_Input(px, fy, pw, 22, "Tile Scale:"); fy += 26;
    Fl_Value_Input *ox = new Fl_Value_Input(px, fy, pw, 22, "Offset X:");   fy += 26;
    Fl_Value_Input *oy = new Fl_Value_Input(px, fy, pw, 22, "Offset Y:");   fy += 26;
    si->range(0.01, 64.0); si->step(0.05); si->value(1.0);
    ox->range(-64.0, 64.0); ox->step(0.05); ox->value(0.0);
    oy->range(-64.0, 64.0); oy->step(0.05); oy->value(0.0);
    si->when(FL_WHEN_CHANGED | FL_WHEN_ENTER_KEY);   /* live while sliding/typing */
    ox->when(FL_WHEN_CHANGED | FL_WHEN_ENTER_KEY);
    oy->when(FL_WHEN_CHANGED | FL_WHEN_ENTER_KEY);
    di->when(FL_WHEN_ENTER_KEY | FL_WHEN_RELEASE);
    faceGroup->end();

    /* Vertex props: X/Y/Z position (vertex mode, exactly one vert). */
    Fl_Group *vertGroup = new Fl_Group(W - PW, TOP, PW, H - TOP);
    int vyy = TOP + 16;
    Fl_Value_Input *vx = new Fl_Value_Input(px, vyy, pw, 22, "X:"); vyy += 28;
    Fl_Value_Input *vy = new Fl_Value_Input(px, vyy, pw, 22, "Y:"); vyy += 28;
    Fl_Value_Input *vz = new Fl_Value_Input(px, vyy, pw, 22, "Z:"); vyy += 28;
    vx->range(-10000, 10000); vy->range(-10000, 10000); vz->range(-10000, 10000);
    vx->step(0.01); vy->step(0.01); vz->step(0.01);
    vx->when(FL_WHEN_CHANGED | FL_WHEN_ENTER_KEY);   /* live while sliding/typing */
    vy->when(FL_WHEN_CHANGED | FL_WHEN_ENTER_KEY);
    vz->when(FL_WHEN_CHANGED | FL_WHEN_ENTER_KEY);
    vertGroup->end();

    /* Entity props (EM3): common fields for any single-selected entity + a
       nested light-only group (colour/intensity/radius) shown for ENT_LIGHT. */
    Fl_Group *entGroup = new Fl_Group(W - PW, TOP, PW, H - TOP);
    int ey = TOP + 12;
    new Fl_Box(W - PW + 8, ey, 60, 18, "Type:");
    Fl_Box *etype = new Fl_Box(px, ey, pw, 18, "");
    etype->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE); etype->labelfont(FL_HELVETICA_BOLD);
    ey += 26;
    Fl_Input *ename = new Fl_Input(px, ey, pw, 22, "Name:");  ey += 26;
    Fl_Input *egrp  = new Fl_Input(px, ey, pw, 22, "Group:"); ey += 28;
    Fl_Value_Input *ex   = new Fl_Value_Input(px, ey, pw, 22, "X:");     ey += 26;
    Fl_Value_Input *eyy  = new Fl_Value_Input(px, ey, pw, 22, "Y:");     ey += 26;
    Fl_Value_Input *ez   = new Fl_Value_Input(px, ey, pw, 22, "Z:");     ey += 26;
    Fl_Value_Input *erot = new Fl_Value_Input(px, ey, pw, 22, "RotY:");  ey += 26;
    Fl_Value_Input *escl = new Fl_Value_Input(px, ey, pw, 22, "Scale:"); ey += 32;
    ex->range(-10000, 10000); eyy->range(-10000, 10000); ez->range(-10000, 10000);
    ex->step(0.01); eyy->step(0.01); ez->step(0.01);
    erot->range(-360, 360); erot->step(1);
    escl->range(0.001, 1000); escl->step(0.01);
    ename->when(FL_WHEN_CHANGED | FL_WHEN_ENTER_KEY);
    egrp->when(FL_WHEN_CHANGED | FL_WHEN_ENTER_KEY);
    ex->when(FL_WHEN_CHANGED | FL_WHEN_ENTER_KEY);
    eyy->when(FL_WHEN_CHANGED | FL_WHEN_ENTER_KEY);
    ez->when(FL_WHEN_CHANGED | FL_WHEN_ENTER_KEY);
    erot->when(FL_WHEN_CHANGED | FL_WHEN_ENTER_KEY);
    escl->when(FL_WHEN_CHANGED | FL_WHEN_ENTER_KEY);

    /* Type-specific sub-groups: all overlap the region below the common fields;
       refreshEntPanel shows the one matching the entity's type. Fields sit at
       absolute Y = ty + header(22) + row*26. Value fields commit live (changed/
       enter); Fl_Choice/Fl_Check_Button fire their callback on change already. */
    int ty = ey;
    const int gh = 190, hx = W - PW + 8, hw = PW - 16;
    const int wLive = FL_WHEN_CHANGED | FL_WHEN_ENTER_KEY;

    Fl_Group *elight = new Fl_Group(W - PW, ty, PW, gh);
    addPanelHeader(hx, ty, hw, "Sphere light");
    Fl_Value_Input *elr   = new Fl_Value_Input(px, ty + 22,  pw, 22, "Red:");
    Fl_Value_Input *elg   = new Fl_Value_Input(px, ty + 48,  pw, 22, "Green:");
    Fl_Value_Input *elb   = new Fl_Value_Input(px, ty + 74,  pw, 22, "Blue:");
    Fl_Value_Input *eli   = new Fl_Value_Input(px, ty + 100, pw, 22, "Intensity:");
    Fl_Value_Input *elrad = new Fl_Value_Input(px, ty + 126, pw, 22, "Radius:");
    elr->range(0,1); elg->range(0,1); elb->range(0,1);
    elr->step(0.05); elg->step(0.05); elb->step(0.05);
    eli->range(0,100); eli->step(0.1); elrad->range(0.1,100); elrad->step(0.5);
    elr->when(wLive); elg->when(wLive); elb->when(wLive); eli->when(wLive); elrad->when(wLive);
    elight->end();

    Fl_Group *gItem = new Fl_Group(W - PW, ty, PW, gh);
    addPanelHeader(hx, ty, hw, "Item");
    Fl_Choice *itType = new Fl_Choice(px, ty + 22, pw, 22, "Kind:");
    itType->add("health"); itType->add("ammo"); itType->add("key");
    gItem->end();

    Fl_Group *gEnemy = new Fl_Group(W - PW, ty, PW, gh);
    addPanelHeader(hx, ty, hw, "Enemy");
    Fl_Value_Input *enHealth = new Fl_Value_Input(px, ty + 22, pw, 22, "Health:");
    Fl_Value_Input *enSpeed  = new Fl_Value_Input(px, ty + 48, pw, 22, "Speed:");
    Fl_Value_Input *enSight  = new Fl_Value_Input(px, ty + 74, pw, 22, "Sight:");
    enHealth->range(0,10000); enHealth->step(1);
    enSpeed->range(0,100); enSpeed->step(0.1); enSight->range(0,1000); enSight->step(0.5);
    enHealth->when(wLive); enSpeed->when(wLive); enSight->when(wLive);
    gEnemy->end();

    Fl_Group *gPlatform = new Fl_Group(W - PW, ty, PW, gh);
    addPanelHeader(hx, ty, hw, "Platform");
    Fl_Input       *plPath  = new Fl_Input(px, ty + 22, pw, 22, "Path:");
    Fl_Choice      *plMove  = new Fl_Choice(px, ty + 48, pw, 22, "Move:");
    plMove->add("once"); plMove->add("ping_pong");
    Fl_Value_Input *plSpeed = new Fl_Value_Input(px, ty + 74, pw, 22, "Speed:");
    plSpeed->range(0,100); plSpeed->step(0.1);
    Fl_Check_Button *plEnabled = new Fl_Check_Button(px, ty + 100, pw, 20, "Enabled");
    Fl_Check_Button *plFace    = new Fl_Check_Button(px, ty + 122, pw, 20, "Face path");
    plPath->when(FL_WHEN_ENTER_KEY | FL_WHEN_RELEASE); plSpeed->when(wLive);
    gPlatform->end();

    Fl_Group *gSwitch = new Fl_Group(W - PW, ty, PW, gh);
    addPanelHeader(hx, ty, hw, "Switch");
    Fl_Input *swTarget = new Fl_Input(px, ty + 22, pw, 22, "Target:");
    swTarget->when(FL_WHEN_ENTER_KEY | FL_WHEN_RELEASE);
    gSwitch->end();

    Fl_Group *gTrigger = new Fl_Group(W - PW, ty, PW, gh);
    addPanelHeader(hx, ty, hw, "Trigger");
    Fl_Value_Input *trSx = new Fl_Value_Input(px, ty + 22, pw, 22, "Size X:");
    Fl_Value_Input *trSy = new Fl_Value_Input(px, ty + 48, pw, 22, "Size Y:");
    Fl_Value_Input *trSz = new Fl_Value_Input(px, ty + 74, pw, 22, "Size Z:");
    Fl_Input       *trTarget = new Fl_Input(px, ty + 100, pw, 22, "Target:");
    Fl_Check_Button *trOnce  = new Fl_Check_Button(px, ty + 126, pw, 20, "Once");
    trSx->range(0,1000); trSy->range(0,1000); trSz->range(0,1000);
    trSx->step(0.1); trSy->step(0.1); trSz->step(0.1);
    trSx->when(wLive); trSy->when(wLive); trSz->when(wLive);
    trTarget->when(FL_WHEN_ENTER_KEY | FL_WHEN_RELEASE);
    gTrigger->end();

    Fl_Group *gDoor = new Fl_Group(W - PW, ty, PW, gh);
    addPanelHeader(hx, ty, hw, "Door");
    Fl_Choice *drMotion = new Fl_Choice(px, ty + 22, pw, 22, "Motion:");
    drMotion->add("slide"); drMotion->add("rotate");
    Fl_Choice *drAxis = new Fl_Choice(px, ty + 48, pw, 22, "Axis:");
    drAxis->add("X"); drAxis->add("Y"); drAxis->add("Z");
    Fl_Value_Input *drAmount = new Fl_Value_Input(px, ty + 74,  pw, 22, "Amount:");
    Fl_Value_Input *drSpeed  = new Fl_Value_Input(px, ty + 100, pw, 22, "Speed:");
    Fl_Value_Input *drAuto   = new Fl_Value_Input(px, ty + 126, pw, 22, "Auto-close:");
    drAmount->range(-360,360); drAmount->step(0.5);
    drSpeed->range(0,1000); drSpeed->step(1); drAuto->range(0,600); drAuto->step(0.5);
    drAmount->when(wLive); drSpeed->when(wLive); drAuto->when(wLive);
    gDoor->end();

    Fl_Group *gPath = new Fl_Group(W - PW, ty, PW, gh);
    addPanelHeader(hx, ty, hw, "Path node");
    Fl_Value_Input *paOrder = new Fl_Value_Input(px, ty + 22, pw, 22, "Order:");
    paOrder->range(0,9999); paOrder->step(1); paOrder->when(wLive);
    gPath->end();

    entGroup->end();

    faceGroup->hide(); vertGroup->hide(); entGroup->hide();  /* refreshPanel reveals one */
    panel->end();
    panel->resizable(NULL);

    /* GL viewport fills the area left of the panel, below menu + toolbar. */
    EditorView *view = new EditorView(0, TOP, W - PW, H - TOP);
    if (argc > 1) view->objPath = argv[1];
    if (argc > 2) view->entPath = argv[2];

    win->end();
    editDpiScaleTree(win);             /* layout above is in 96-DPI pixels */
    win->resizable(view);              /* only the viewport grows/shrinks */

    /* Wire the toolbar buttons to the view, then light the initial mode. */
    view->bVert = bVert; view->bEdge = bEdge; view->bFace = bFace; view->bEnt = bEnt;
    bVert->callback(modeButtonCb, view);
    bEdge->callback(modeButtonCb, view);
    bFace->callback(modeButtonCb, view);
    bEnt->callback(modeButtonCb, view);
    view->applyMode(SEL_VERT);

    /* Wire the property panel (widgets built above, in the panel groups). */
    view->propPanel = panel; view->faceGroup = faceGroup; view->vertGroup = vertGroup;
    view->matChoice = mc; view->diffuseInput = di;
    view->scaleInput = si; view->offXInput = ox; view->offYInput = oy;
    view->vxInput = vx; view->vyInput = vy; view->vzInput = vz;
    mc->callback(matChoiceCb, view);
    ab->callback(addMatCb, view);
    di->callback(diffuseCb, view);
    si->callback(tilingCb, view);
    ox->callback(tilingCb, view);
    oy->callback(tilingCb, view);
    vx->callback(vertPosCb, view);
    vy->callback(vertPosCb, view);
    vz->callback(vertPosCb, view);

    /* Wire the entity form (EM3). */
    view->entGroup = entGroup; view->entLightGroup = elight; view->entTypeBox = etype;
    view->entNameIn = ename; view->entGroupIn = egrp;
    view->entXIn = ex; view->entYIn = eyy; view->entZIn = ez;
    view->entRotIn = erot; view->entScaleIn = escl;
    view->entLrIn = elr; view->entLgIn = elg; view->entLbIn = elb;
    view->entLiIn = eli; view->entLradIn = elrad;
    ename->callback(entFieldCb, view); egrp->callback(entFieldCb, view);
    ex->callback(entFieldCb, view); eyy->callback(entFieldCb, view); ez->callback(entFieldCb, view);
    erot->callback(entFieldCb, view); escl->callback(entFieldCb, view);
    elr->callback(entFieldCb, view); elg->callback(entFieldCb, view); elb->callback(entFieldCb, view);
    eli->callback(entFieldCb, view); elrad->callback(entFieldCb, view);
    view->gItem = gItem; view->gEnemy = gEnemy; view->gPlatform = gPlatform;
    view->gSwitch = gSwitch; view->gTrigger = gTrigger; view->gDoor = gDoor; view->gPath = gPath;
    view->itType = itType;
    view->enHealth = enHealth; view->enSpeed = enSpeed; view->enSight = enSight;
    view->plPath = plPath; view->plMove = plMove; view->plSpeed = plSpeed;
    view->plEnabled = plEnabled; view->plFace = plFace;
    view->swTarget = swTarget;
    view->trSx = trSx; view->trSy = trSy; view->trSz = trSz;
    view->trTarget = trTarget; view->trOnce = trOnce;
    view->drMotion = drMotion; view->drAxis = drAxis;
    view->drAmount = drAmount; view->drSpeed = drSpeed; view->drAuto = drAuto;
    view->paOrder = paOrder;
    itType->callback(entFieldCb, view);
    enHealth->callback(entFieldCb, view); enSpeed->callback(entFieldCb, view); enSight->callback(entFieldCb, view);
    plPath->callback(entFieldCb, view); plMove->callback(entFieldCb, view); plSpeed->callback(entFieldCb, view);
    plEnabled->callback(entFieldCb, view); plFace->callback(entFieldCb, view);
    swTarget->callback(entFieldCb, view);
    trSx->callback(entFieldCb, view); trSy->callback(entFieldCb, view); trSz->callback(entFieldCb, view);
    trTarget->callback(entFieldCb, view); trOnce->callback(entFieldCb, view);
    drMotion->callback(entFieldCb, view); drAxis->callback(entFieldCb, view);
    drAmount->callback(entFieldCb, view); drSpeed->callback(entFieldCb, view); drAuto->callback(entFieldCb, view);
    paOrder->callback(entFieldCb, view);

    /* Menu items (added after `view` exists for the Edit callbacks). Stash the
       Undo/Redo item indices so the view can grey them out when empty. */
    menu->add("File/Save",          FL_COMMAND + 's', menuSaveCb,   view);
    menu->add("File/Open...",       FL_COMMAND + 'o', menuOpenCb,   view);
    menu->add("File/Export OBJ...", 0,                menuExportCb, view);
    menu->add("File/Exit",          0,                menuExitCb,   win);
    view->menuBar = menu;
    view->undoIdx = menu->add("Edit/Undo", FL_COMMAND + 'z', menuUndoCb, view);
    view->redoIdx = menu->add("Edit/Redo", FL_COMMAND + 'y', menuRedoCb, view);
    view->updateMenuEnabled();             /* both start greyed (empty stacks) */

    menu->add("Add/Cube",          0,         menuAddCubeCb,  view);
    menu->add("Add/Plane",         0,         menuAddPlaneCb, view);
    menu->add("Mesh/Extrude",         'e',       menuExtrudeCb,  view);
    menu->add("Mesh/Make Face",       'f',       menuMakeFaceCb, view);
    menu->add("Mesh/Flip Normals",    0,         menuFlipCb,     view);
    menu->add("Mesh/Recalc Normals",  'n',       menuRecalcCb,   view);
    menu->add("Mesh/Merge Verts",     'm',       menuMergeCb,    view);
    menu->add("Mesh/Delete",          FL_Delete, menuDeleteCb,   view);
    editMenuPad(menu);                     /* last: add() may reallocate the items */

    win->callback(winCloseCb);             /* confirm on the window close button too */

    conLogf("SOOB Level Editor — right-drag look, WASD move, PgUp/PgDn up/down, wheel dolly\n");
    conLogf("  left-click = select, Shift+left-click = add/toggle, 1/2/3/4 = vertex/edge/face/entity mode\n");
    conLogf("  G = grab (X/Y/Z lock), R = rotate entities (yaw, Shift=fine), click/Enter confirm, Esc cancel\n");
    conLogf("  Ctrl+Z / Ctrl+Y = undo/redo\n");
    conLogf("  E = extrude, F = make face (3-4 verts), N = recalc normals, M = merge verts, X/Del = delete\n");
    conLogf("  Ctrl+S save, Ctrl+O open, File > Export OBJ (.lvl native; OBJ+MTL for engine/Blender)\n");
    conLogf("Loading %s / %s (run from repo root)\n", view->objPath, view->entPath);

    /* Boot splash: a frameless window (logo + status line) centred on screen,
       shown while the main window's heavy first-frame load runs. Sized to the
       image; the status line overlays its bottom strip. */
    Fl_Image *splashPix = editDpiImage(new Fl_Pixmap(splash_xpm));
    int sw = splashPix->w(), sh = splashPix->h();
    gSplash = new Fl_Window((Fl::w() - sw) / 2, (Fl::h() - sh) / 2, sw, sh);
    gSplash->border(0);
    gSplash->begin();
        Fl_Box *splashImg = new Fl_Box(0, 0, sw, sh);
        splashImg->box(FL_NO_BOX);
        splashImg->image(splashPix);
        gSplashText = new Fl_Box(0, sh - editDpi(28), sw, editDpi(22));
        gSplashText->box(FL_NO_BOX);   /* label only, no fill over the image */
        gSplashText->align(FL_ALIGN_INSIDE | FL_ALIGN_CENTER);
        gSplashText->labelfont(FL_HELVETICA_BOLD);
        gSplashText->labelsize(editDpi(12));
        gSplashText->labelcolor(FL_WHITE);
        gSplashText->copy_label("Starting...");
    gSplash->end();
    gSplash->show();
    Fl::check();                       /* paint the splash before the load blocks */

    if (!editPickGlMode(view)) {
        conLogf("editor: FATAL - the driver offers no usable GL mode.\n");
        return 1;
    }

    win->show(argc, argv);             /* GL context becomes valid here */

    /* Let the window actually be realised before any GL call. show() only
       posts the creation; on Win9x the child GL window's HWND and DC are not
       necessarily usable until the message queue has been pumped once. */
    Fl::check();

    /* Confirm the GL window really came up. On an unsupported mode FLTK logs
       through Fl::error and returns with no window created, leaving nothing to
       draw into -- previously the editor then sat in Fl::run() redrawing a
       window that did not exist until the machine had to be restarted. Fail
       here instead. */
    if (!view->shown()) {
        conLogf("editor: FATAL - GL window not created; see FLTK: lines above.\n");
        return 1;
    }

    view->take_focus();
    gSplash->show();                   /* re-raise above the just-shown main window */

    /* Do NOT call make_current()/bootstrap() by hand here. Fl_Gl_Window::flush()
       calls make_current() itself during a real paint, once the window is fully
       realised -- and on Win9x that timing matters: called straight after
       show(), SetPixelFormat on the child window's DC fails with an invalid
       handle and no context is ever created. draw() runs bootstrap() on the
       first live frame (see draw()), so pumping the event loop performs the
       load with a context that is genuinely current, and splashStatus() keeps
       ticking the splash exactly as before. */
    for (int spin = 0; spin < 400 && !view->bootstrapped; spin++)
        Fl::wait(0.05);

    if (!view->bootstrapped) {
        conLogf("editor: FATAL - no first frame within 20s;"
                " the GL context never became current.\n");
        conLogf("editor:   Fl_Gl_Window::context() = %p\n",
                (void *)view->context());
        return 1;
    }

    gSplash->hide();
    win->redraw();                     /* repaint the area the splash covered */
    delete gSplash; gSplash = 0; gSplashText = 0;
    delete splashPix;

    Fl::add_timeout(1.0 / 60.0, frameTimer, view);

    return Fl::run();
}
