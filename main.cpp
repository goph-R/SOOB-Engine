#ifdef _WIN32
#include <SDL/SDL.h>
#else
#include <SDL.h>
#endif
#include <GL/gl.h>
#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Forward-declare conLogf so every engine header included below can call
   it before console.h (which needs ui.h + texture.h for conRender) is
   parsed. Same-TU static forward decl + later static definition is
   valid C/C++ — all the module headers compile as part of main.cpp's
   single translation unit. */
static void conLogf(const char *fmt, ...);

/* SOOB-Engine keeps the original 540-unit virtual canvas (text constants in
   menu.h / console.h / main.cpp HUD assume this height). Override before
   ui.h is parsed; engine default is 480 (which is what Find5 ships at). */
#define UI_VIRTUAL_H 540.0f

#include "obj_loader.h"
#include "texture.h"             /* SOOB-Core */
#include "ui.h"                  /* SOOB-Core */
#include "sound.h"               /* SOOB-Core */
#include "music.h"               /* SOOB-Core */
#include "iqm.h"
#include "asset_registry.h"      /* SOOB-Core */
#include "entity.h"
#include "flashlight.h"
#include "physics.h"
#include "nav.h"
#include "path.h"
#include "console.h"
#include "script.h"              /* SOOB-Core */
#include "script_ext.h"          /* SOOB-Engine-side bindings (ent_activate) */
#include "game.h"
#include "game_session.h"        /* gameInit / gameFree — needs script.h above */
#include "menu.h"
#include "app_ext.h"             /* SOOB-Engine-side app_* bindings (needs AppState from menu.h) */
#include "config.h"              /* SOOB-Core */

#define SAMPLE_RATE 44100

/* Screen size — defaults overridden by -w / -h command-line args. */
static int SCREEN_W = 640;
static int SCREEN_H = 480;

/* Horizontal FOV held constant; vertical FOV is derived from the current
   aspect ratio so wider screens show more to the sides rather than stretch.
   H = 90° matches the classic Quake/HL1 feel at 4:3. */
#define H_FOV_DEG 90.0f
static float computeVFov(void)
{
    float hRad = H_FOV_DEG * (float)M_PI / 180.0f;
    float aspect = (float)SCREEN_W / (float)SCREEN_H;
    float vRad = 2.0f * atanf(tanf(hRad * 0.5f) / aspect);
    return vRad * 180.0f / (float)M_PI;
}

/* Multitexture setup, camera helpers, and the level draw path now live in
   render_level.h (shared with the SOOB level editor). It's included below,
   after the module headers that define Vec3 / ObjMesh / TexCache. */

/* ---- Windows refresh-rate preservation ----
 *
 * SDL 1.2's fullscreen path calls ChangeDisplaySettings without specifying
 * a frequency, so the driver falls back to its per-mode default — which on
 * Win9x/2000 is typically 60Hz regardless of what the user had configured.
 * We sample the desktop's configured rate before SDL touches the display
 * (via ENUM_REGISTRY_SETTINGS so we read the persisted rate rather than
 * whatever happens to be active), then after SDL_SetVideoMode we re-apply
 * that rate on top of SDL's chosen resolution. No-op on non-Win32. */
#ifdef _WIN32
#include <windows.h>
static DWORD g_savedDesktopHz = 0;

static void saveDesktopRefreshHz(void)
{
    DEVMODE dm;
    memset(&dm, 0, sizeof(dm));
    dm.dmSize = sizeof(dm);
    if (EnumDisplaySettings(NULL, ENUM_REGISTRY_SETTINGS, &dm)) {
        g_savedDesktopHz = dm.dmDisplayFrequency;
        conLogf("Display: desktop refresh rate is %lu Hz\n",
               (unsigned long)g_savedDesktopHz);
    } else {
        conLogf("Display: EnumDisplaySettings failed\n");
    }
}

static void applyFullscreenRefreshHz(int width, int height)
{
    /* 0 = not queried yet, 1 = "default" hardware rate — skip in either case
       (setting to 1 would force the driver back to the very thing we're
       trying to override). */
    if (g_savedDesktopHz <= 1) return;

    DEVMODE dm;
    memset(&dm, 0, sizeof(dm));
    dm.dmSize             = sizeof(dm);
    dm.dmPelsWidth        = width;
    dm.dmPelsHeight       = height;
    dm.dmDisplayFrequency = g_savedDesktopHz;
    dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;

    LONG rc = ChangeDisplaySettingsEx(NULL, &dm, NULL, CDS_FULLSCREEN, NULL);
    if (rc == DISP_CHANGE_SUCCESSFUL) {
        conLogf("Display: refresh set to %lu Hz (fullscreen %dx%d)\n",
               (unsigned long)g_savedDesktopHz, width, height);
    } else {
        conLogf("Display: could not force %lu Hz at %dx%d (rc=%ld); "
                "staying at SDL's default rate\n",
                (unsigned long)g_savedDesktopHz, width, height, (long)rc);
    }
}
#else
static void saveDesktopRefreshHz(void) {}
static void applyFullscreenRefreshHz(int, int) {}
#endif

/* DPI awareness — included after <windows.h> so its careful late ordering is
   preserved; self-guarded and a no-op on non-Win32. */
#include "dpi.h"

/* ---- Level render module (multitexture + camera helpers + level draw) ----
   Shared verbatim with the SOOB level editor. Included here rather than
   defined inline; needs Vec3 / ObjMesh / TexCache (obj_loader.h, texture.h)
   and conLogf, all declared above. */
#include "render_level.h"
#include "render_world.h"      /* renderWorld() + Camera — needs Game/entRender above */

/* ---- Crosshair ---- */

static void renderCrosshair(void)
{
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, SCREEN_W, SCREEN_H, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);

    float cx = SCREEN_W / 2.0f;
    float cy = SCREEN_H / 2.0f;
    float size = 10.0f;

    glLineWidth(2.0f);
    glColor3f(0.0f, 1.0f, 0.0f);
    glBegin(GL_LINES);
        glVertex2f(cx - size, cy);
        glVertex2f(cx + size, cy);
        glVertex2f(cx, cy - size);
        glVertex2f(cx, cy + size);
    glEnd();

    glEnable(GL_DEPTH_TEST);

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
}

/* ---- Simple gun rendering (first person) ---- */

static void renderGun(int flashTimer)
{
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glSetPerspective(60.0f, (float)SCREEN_W / SCREEN_H, 0.01f, 10.0f);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_LIGHTING);
    glClear(GL_DEPTH_BUFFER_BIT);

    glTranslatef(0.3f, -0.3f, -0.6f);
    glRotatef(-5.0f, 0, 0, 1);

    glColor3f(0.2f, 0.2f, 0.25f);
    glBegin(GL_QUADS);
        /* Top */
        glVertex3f(-0.05f, 0.05f, -0.2f);
        glVertex3f( 0.05f, 0.05f, -0.2f);
        glVertex3f( 0.05f, 0.05f,  0.1f);
        glVertex3f(-0.05f, 0.05f,  0.1f);
        /* Bottom */
        glVertex3f(-0.05f, -0.05f, -0.2f);
        glVertex3f(-0.05f, -0.05f,  0.1f);
        glVertex3f( 0.05f, -0.05f,  0.1f);
        glVertex3f( 0.05f, -0.05f, -0.2f);
        /* Left */
        glVertex3f(-0.05f, -0.05f, -0.2f);
        glVertex3f(-0.05f,  0.05f, -0.2f);
        glVertex3f(-0.05f,  0.05f,  0.1f);
        glVertex3f(-0.05f, -0.05f,  0.1f);
        /* Right */
        glVertex3f(0.05f, -0.05f, -0.2f);
        glVertex3f(0.05f, -0.05f,  0.1f);
        glVertex3f(0.05f,  0.05f,  0.1f);
        glVertex3f(0.05f,  0.05f, -0.2f);
        /* Front (barrel end) */
        glVertex3f(-0.05f, -0.05f, -0.2f);
        glVertex3f( 0.05f, -0.05f, -0.2f);
        glVertex3f( 0.05f,  0.05f, -0.2f);
        glVertex3f(-0.05f,  0.05f, -0.2f);
        /* Back */
        glVertex3f(-0.05f, -0.05f,  0.1f);
        glVertex3f(-0.05f,  0.05f,  0.1f);
        glVertex3f( 0.05f,  0.05f,  0.1f);
        glVertex3f( 0.05f, -0.05f,  0.1f);
    glEnd();

    /* Muzzle flash */
    if (flashTimer > 0) {
        glColor3f(1.0f, 0.8f, 0.2f);
        glBegin(GL_TRIANGLES);
            glVertex3f( 0.0f,  0.0f, -0.22f);
            glVertex3f(-0.08f, 0.08f, -0.35f);
            glVertex3f( 0.08f, 0.08f, -0.35f);
            glVertex3f( 0.0f,  0.0f, -0.22f);
            glVertex3f(-0.06f,-0.02f, -0.32f);
            glVertex3f( 0.06f,-0.02f, -0.32f);
        glEnd();
    }

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
}

/* Is the player standing on any kinematic body owned by a platform in the
   leader's Entity.group? Raycast straight down from the capsule's center;
   if the closest hit is one of the group's bodies and within capsule
   half-height + radius + 0.10m, the player is riding. */
static int playerRidingGroup(PhysWorld *pw, EntityList *el, Entity *leader)
{
    if (!pw->ghostObject || !pw->capsuleShape) return 0;
    btVector3 from = pw->ghostObject->getWorldTransform().getOrigin();
    float reach = (float)(pw->capsuleShape->getHalfHeight()
                        + pw->capsuleShape->getRadius()) + 0.10f;
    btVector3 to = from + btVector3(0, -reach, 0);

    btCollisionWorld::ClosestRayResultCallback cb(from, to);
    cb.m_collisionFilterMask = btBroadphaseProxy::DefaultFilter;
    pw->world->rayTest(from, to, cb);
    if (!cb.hasHit()) return 0;

    for (int i = 0; i < el->count; i++) {
        Entity *m = &el->entities[i];
        if (!m->active || m->type != ENT_PLATFORM) continue;
        if (!m->physBody) continue;
        if (strcmp(m->group, leader->group) != 0) continue;
        if ((const btCollisionObject *)m->physBody == cb.m_collisionObject) return 1;
    }
    return 0;
}

/* ---- Platform update ----
 * Walks each leader platform forward along its path by speed*dt. Computes
 * a position delta this frame, applies it to the leader, all siblings in
 * the same Entity.group, and any player riding the group. Collider
 * transforms are teleported (no sweep) since a standing rider would
 * always block one.
 *
 * Runs BEFORE physStep so the kinematic collider position and (later) the
 * rider's ghost position both land in the same tick — Bullet's character
 * controller stepDown then sees the new contact naturally with no popping.
 *
 * Step 4 of docs/PLAN_PATH_PLATFORMS.md — translation only. Rotation
 * (face_path) and carry-the-rider are added in Steps 5–6.
 */
static void updatePlatforms(EntityList *el, PathTable *pt, PhysWorld *pw, float dt)
{
    if (!el || !pt) return;
    for (int i = 0; i < el->count; i++) {
        Entity *e = &el->entities[i];
        if (!e->active || e->type != ENT_PLATFORM) continue;
        if (!e->platform.isLeader) continue;
        if (!e->platform.enabled) continue;
        if (e->platform.finished) continue;

        PathGroup *pg = pathTableFind(pt, e->platform.pathGroup);
        if (!pg || pg->nodeCount < 2) continue;

        Vec3 oldPos = pathSample(pg, el, e->platform.segIdx, e->platform.segT);
        float oldYaw = e->rotY;
        pathAdvance(pg, e, dt);
        Vec3 newPos = pathSample(pg, el, e->platform.segIdx, e->platform.segT);

        /* Yaw-only heading along the current segment. Vertical-only segments
           (no XZ component) preserve the previous heading so an elevator
           doesn't tip sideways. */
        float newYaw = oldYaw;
        if (e->platform.facePath) {
            Vec3 a = pathSample(pg, el, e->platform.segIdx, 0.0f);
            Vec3 b = pathSample(pg, el, e->platform.segIdx, 1.0f);
            float segDx = b.x - a.x, segDz = b.z - a.z;
            if (e->platform.dir < 0) { segDx = -segDx; segDz = -segDz; }
            if (segDx*segDx + segDz*segDz > 1e-6f) {
                float pathYaw = atan2f(segDx, segDz) * 180.0f / (float)M_PI;
                newYaw = pathYaw + e->platform.rotOffset;
            }
        }

        float dx = newPos.x - oldPos.x;
        float dy = newPos.y - oldPos.y;
        float dz = newPos.z - oldPos.z;
        float dyaw = newYaw - oldYaw;
        if (fabsf(dx) < 1e-6f && fabsf(dy) < 1e-6f && fabsf(dz) < 1e-6f
            && fabsf(dyaw) < 1e-6f) continue;

        /* Snapshot the rider's pre-move ghost origin so we can rotate their
           offset from the leader's pivot when the platform yaws. Detection
           uses the OLD collider position (we haven't moved it yet). */
        int riding = playerRidingGroup(pw, el, e);
        btVector3 playerOldOrigin(0, 0, 0);
        if (riding) playerOldOrigin = pw->ghostObject->getWorldTransform().getOrigin();

        /* Leader transform. */
        e->posX = newPos.x; e->posY = newPos.y; e->posZ = newPos.z;
        e->rotY = newYaw;
        float rad = e->rotY * (float)M_PI / 180.0f;
        float cs = cosf(rad), sn = sinf(rad);
        Vec3 lwc;
        lwc.x = e->posX + (cs * e->platform.lcx + sn * e->platform.lcz);
        lwc.y = e->posY + e->platform.lcy;
        lwc.z = e->posZ + (-sn * e->platform.lcx + cs * e->platform.lcz);
        physSetKinematicBoxTransform(pw, e->physBody, lwc, e->rotY);

        /* Siblings: rotate stored offset by leader's current yaw, place, and
           yaw siblings by leader's yaw + their captured sibLocalAngle so the
           cluster rotates rigidly. */
        for (int j = 0; j < el->count; j++) {
            Entity *m = &el->entities[j];
            if (!m->active || m->type != ENT_PLATFORM) continue;
            if (m->platform.isLeader) continue;
            if (m->platform.leaderIdx != i) continue;

            m->posX = newPos.x + (cs * m->platform.offX + sn * m->platform.offZ);
            m->posY = newPos.y +  m->platform.offY;
            m->posZ = newPos.z + (-sn * m->platform.offX + cs * m->platform.offZ);
            m->rotY = newYaw + m->platform.sibLocalAngle;

            float mrad = m->rotY * (float)M_PI / 180.0f;
            float mcs = cosf(mrad), msn = sinf(mrad);
            Vec3 mwc;
            mwc.x = m->posX + (mcs * m->platform.lcx + msn * m->platform.lcz);
            mwc.y = m->posY +  m->platform.lcy;
            mwc.z = m->posZ + (-msn * m->platform.lcx + mcs * m->platform.lcz);
            physSetKinematicBoxTransform(pw, m->physBody, mwc, m->rotY);
        }

        /* Carry the rider: rotate their offset from the leader's pivot by
           dyaw, then translate by the position delta. Mouse-look yaw is
           intentionally NOT touched — that would fight the player's view.
           Source-engine convention: rotating platforms move you around but
           don't spin your camera. */
        if (riding) {
            btVector3 leadOld(oldPos.x, oldPos.y, oldPos.z);
            btVector3 leadNew(newPos.x, newPos.y, newPos.z);
            btVector3 off = playerOldOrigin - leadOld;
            float r = dyaw * (float)M_PI / 180.0f;
            float c = cosf(r), s = sinf(r);
            btVector3 offRot(c * off.x() + s * off.z(),
                             off.y(),
                            -s * off.x() + c * off.z());
            btTransform t = pw->ghostObject->getWorldTransform();
            t.setOrigin(leadNew + offRot);
            pw->ghostObject->setWorldTransform(t);
        }
    }
}

/* ---- Door update ----
 * Advances each opening/closing door by speed*dt along its path. Asks the
 * physics layer to sweep its kinematic body from current to target; if the
 * sweep hits anything (player, decoration, other door), the door pauses
 * this tick and retries next tick. When progress reaches 0 or 1, the door
 * settles into the closed/open state.
 */
static void updateDoors(EntityList *el, PhysWorld *pw, float dt)
{
    for (int i = 0; i < el->count; i++) {
        Entity *e = &el->entities[i];
        if (!e->active || e->type != ENT_DOOR || !e->physBody) continue;

        /* Auto-close: if door is fully open and auto_close was set, count up.
           When the timer exceeds the configured duration, transition to
           closing. Blocked closes don't reset the timer — it's advisory. */
        if (e->door.state == 2 && e->door.autoCloseTime > 0.0f) {
            e->door.openTimer += dt;
            if (e->door.openTimer >= e->door.autoCloseTime) {
                e->door.state = 3;
                e->door.openTimer = 0.0f;
            }
        }

        if (e->door.state != 1 && e->door.state != 3) continue;

        float delta = (e->door.speed * dt) / e->door.amount;
        float target = e->door.progress + (e->door.state == 1 ? delta : -delta);
        if (target > 1.0f) target = 1.0f;
        if (target < 0.0f) target = 0.0f;

        /* Target entity transform derived from the closed pose + progress. */
        float nx = e->door.closedX, ny = e->door.closedY, nz = e->door.closedZ;
        float nr = e->door.closedRotY;
        if (e->door.motion == 0) {
            float off = target * e->door.amount;
            if      (e->door.axis == 0) nx += off;
            else if (e->door.axis == 1) ny += off;
            else                        nz += off;
        } else {
            nr += target * e->door.amount;
        }

        /* Collider world center = entity position + rotated local AABB offset. */
        float rad = nr * (float)M_PI / 180.0f;
        float cs = cosf(rad), sn = sinf(rad);
        float cx = nx + (cs * e->door.lcx + sn * e->door.lcz);
        float cy = ny + e->door.lcy;
        float cz = nz + (-sn * e->door.lcx + cs * e->door.lcz);

        Vec3 mc = { cx, cy, cz };
        if (physMoveKinematicBox(pw, e->physBody, mc, nr)) {
            e->door.progress = target;
            e->posX = nx; e->posY = ny; e->posZ = nz; e->rotY = nr;
            if (target >= 1.0f) {
                e->door.state = 2;
                e->door.openTimer = 0.0f;
            }
            else if (target <= 0.0f) e->door.state = 0;
        } else {
            /* Blocked — print once per activation session so the user
               knows the sweep is rejecting. Usually means the collider
               AABB intersects world geometry (wall, floor, another
               decoration). Press B to see the wireframe. */
            static int lastLoggedEnt = -1;
            if (lastLoggedEnt != i) {
                conLogf("door: '%s' blocked (sweep hit — collider overlapping "
                       "world geometry?)\n", e->name);
                lastLoggedEnt = i;
            }
        }
    }
}

/* ---- Main ---- */

int main(int argc, char *argv[])
{
    /* Tell modern Windows we render in real pixels, before SDL touches the
       display — otherwise a non-100% display scale makes SDL_GetVideoInfo
       report a virtualised desktop and fullscreen oversizes. Safe no-op on
       old Windows (and Linux): see dpi.h. */
    dpiSetProcessAware();

    /* Display config: built-in defaults → config.lua → CLI args → clamp.
       Width/height of 0 is a sentinel for "use desktop resolution",
       resolved below once SDL knows the desktop size. */
    Config cfg = configLoadDefaults();
    configLoadFromFile(&cfg, "config.lua");
    configApplyArgs(&cfg, argc, argv);
    configClamp(&cfg);
    SCREEN_W = cfg.width;
    SCREEN_H = cfg.height;
    int fullscreen = cfg.fullscreen;

    /* Sample the configured desktop refresh rate BEFORE SDL touches the
       display, so later we can reapply it over SDL's fullscreen mode
       (which otherwise drops to the driver's 60Hz default on Win9x). */
    saveDesktopRefreshHz();

    /* Seed rand() once. Used by sndLibPick for non-repeating sound variant
       picks; further uses can rely on it without re-seeding. */
    srand((unsigned)time(NULL));

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        conLogf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    /* Resolve the "0 = desktop" sentinel for fullscreen mode.
       SDL_GetVideoInfo()->current_w/h returns the desktop size BEFORE
       SDL_SetVideoMode has been called (SDL 1.2.10+). */
    if (fullscreen) {
        const SDL_VideoInfo *vi = SDL_GetVideoInfo();
        if (vi) {
            if (SCREEN_W == 0) SCREEN_W = vi->current_w;
            if (SCREEN_H == 0) SCREEN_H = vi->current_h;
        }
    }
    /* Final clamp: catches any 0-sentinel that survived (e.g. windowed
       mode with width=0 in config.lua, or SDL_GetVideoInfo returning
       NULL) and any out-of-range desktop value. */
    if (SCREEN_W < CONFIG_W_MIN) SCREEN_W = CONFIG_W_MIN;
    if (SCREEN_W > CONFIG_W_MAX) SCREEN_W = CONFIG_W_MAX;
    if (SCREEN_H < CONFIG_H_MIN) SCREEN_H = CONFIG_H_MIN;
    if (SCREEN_H > CONFIG_H_MAX) SCREEN_H = CONFIG_H_MAX;
    conLogf("Resolution: %dx%d%s (V-FOV %.1f deg for %d deg H-FOV)\n",
           SCREEN_W, SCREEN_H, fullscreen ? " fullscreen" : "",
           computeVFov(), (int)H_FOV_DEG);

    SoundSystem snd;
    if (!sndInit(&snd, SAMPLE_RATE)) {
        SDL_Quit();
        return 1;
    }

    SoundLibrary sndLib;
    sndLibInit(&sndLib);
    /* Sounds are registered from assets.lua once the Lua runtime boots
       (further down, after UI + entities exist). sndLib is empty until
       then — nothing tries to play before the main loop. */

    /* Streaming music subsystem — separate AL sources from the SFX pool,
       crossfade-capable, lives at app scope so menu/game transitions
       don't tear it down. Library is name->path; .ogg files are opened
       lazily by musicPlay. */
    MusicSystem mus;
    musicInit(&mus);
    MusicLibrary musLib;
    musicLibInit(&musLib);

    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    /* Request vsync before SDL_SetVideoMode (SDL 1.2.10+). Only a request:
       some old drivers ignore it and there's no read-back in 1.2, so we just
       log what we asked for. */
    SDL_GL_SetAttribute(SDL_GL_SWAP_CONTROL, cfg.vsync);
    conLogf("VSync: %s\n", cfg.vsync ? "on (requested)" : "off");

    Uint32 videoFlags = SDL_OPENGL;
    if (fullscreen) videoFlags |= SDL_FULLSCREEN;
    SDL_Surface *screen = SDL_SetVideoMode(SCREEN_W, SCREEN_H, 32, videoFlags);
    if (!screen) {
        conLogf("SDL_SetVideoMode failed: %s\n", SDL_GetError());
        sndShutdown(&snd);
        SDL_Quit();
        return 1;
    }

    /* Re-apply the desktop refresh rate on top of SDL's fullscreen mode. */
    if (fullscreen) applyFullscreenRefreshHz(SCREEN_W, SCREEN_H);

    /* Enable translated Unicode characters on KEYDOWN events so the dev
       console can read typed characters via event.key.keysym.unicode. */
    SDL_EnableUNICODE(1);

    SDL_WM_SetCaption("FPS Demo - SDL + OpenGL + Bullet + OpenAL", NULL);
    SDL_WM_GrabInput(SDL_GRAB_ON);
    SDL_ShowCursor(SDL_DISABLE);

    /* OpenGL setup */
    glViewport(0, 0, SCREEN_W, SCREEN_H);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glSetPerspective(computeVFov(), (float)SCREEN_W / SCREEN_H, 0.1f, 200.0f);
    glMatrixMode(GL_MODELVIEW);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    /* Lighting (used as fallback when no lightmap) */
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    float lightPos[] = { 0.0f, 10.0f, 0.0f, 1.0f };
    float lightAmb[] = { 0.3f, 0.3f, 0.3f, 1.0f };
    float lightDif[] = { 0.7f, 0.7f, 0.7f, 1.0f };
    glLightfv(GL_LIGHT0, GL_POSITION, lightPos);
    glLightfv(GL_LIGHT0, GL_AMBIENT, lightAmb);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, lightDif);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);

    /* Init multitexture extension */
    initMultitexture(SDL_GL_GetProcAddress);

    /* Init UI / HUD (builds bitmap font atlas as a GL texture) */
    UiState ui;
    uiInit(&ui, SCREEN_W, SCREEN_H);

    /* Dev console — rolls down with backtick in-game. Bound globally up
       front so every conLogf call after this point (including asset /
       script loading chatter below) also lands in the scrollback. */
    Console con;
    conInit(&con);
    conBind(&con);

    /* App state machine — created early so its menuTex is available to
       scriptInit as the shared texture cache (Lua drawRegion / drawBg /
       drawBlur all need a cache; reusing app.menuTex means no extra
       cache instance and one set of GL textures across menu + scripts). */
    AppState app;
    appInit(&app, SCREEN_W, SCREEN_H, &ui, NULL, NULL, &snd, &sndLib, &mus, &musLib);

    /* App-level blur cache for Lua drawBlur. Separate from menuTex
       because TexBlurCache stores downsampled summaries, not full
       texture uploads. */
    TexBlurCache blurCache;
    texBlurInit(&blurCache);

    /* App-level Lua runtime + asset registry. Loaded once before any game
       boots so .ent files can resolve model / texture logical names and
       sound / font tables can populate their libraries. */
    AssetRegistry assetReg;
    assetRegInit(&assetReg);
    ScriptSystem script;
    /* Per-user persistence path: AppData\SOOB-Engine\soob.dat (Windows) or
       ~/.config/SOOB-Engine/soob.dat (Unix). Falls back to next-to-exe on
       Win98 (no %APPDATA%). Static so the buffer outlives script. */
    static char optPath[512];
    scriptResolveConfigPath("SOOB-Engine", "soob.dat", optPath, sizeof(optPath));
    scriptInit(&script, &ui, &snd, &sndLib, &mus, &musLib, &assetReg,
               &app.menuTex, &blurCache, optPath);
    scriptExtRegister(&script);          /* SOOB-Engine-only: ent_activate, conExecute */
    appExtRegister(&script);             /* SOOB-Engine-only: appNewGame / continue / quit / has_game */
    scriptLoadAssets(&script, "assets.lua");
    scriptInstallConsolePrint(&script);  /* override Lua print → console */

    /* Backfill the AssetRegistry + ScriptSystem pointers on AppState now
       that they exist. Tolerates the early appInit above (which was
       only run early so app.menuTex was usable by scriptInit). */
    app.assetReg = &assetReg;
    app.script   = &script;
    appExtSetApp(&app);                  /* wire AppState into app_* bindings */

    /* Run the entry script ONCE at app boot — it installs the scene-stack
       hooks and pushes the initial menu scene. Per-session work (HUD
       welcome message, etc.) lives in onStart which gameInit fires. */
    scriptRunFile(&script, "scripts/main.lua");

    /* Game struct is allocated once on the stack; gameInited tracks
       whether it currently holds an active session (so gameFree only
       runs on one that was initialized). */
    Game game;
    int  gameInited = 0;

    /* Start in menu mode: release the mouse so the cursor can roam the UI. */
    SDL_WM_GrabInput(SDL_GRAB_OFF);
    SDL_ShowCursor(SDL_ENABLE);

    const float moveSpeed = 6.0f;

    Uint32 lastTime = SDL_GetTicks();
    SDL_Event event;

    while (app.running) {
        Uint32 now = SDL_GetTicks();
        float dt = (now - lastTime) / 1000.0f;
        if (dt > 0.15f) dt = 0.15f;
        lastTime = now;

        uiUpdateMessage(&ui, dt);

        /* Stream music regardless of mode — runs through menus, console
           pause, and the synchronous gameInit below (the ring's lookahead
           absorbs the load stall). */
        musicUpdate(&mus, dt);

        if (app.mode == MODE_MENU) {
            /* ---- MODE_MENU: Lua scene stack owns input + render ---- */
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) {
                    app.running = 0;
                    break;
                }
                if (event.type == SDL_KEYDOWN) {
                    const char *name = SDL_GetKeyName(event.key.keysym.sym);
                    scriptCallKeyDown(&script, name ? name : "");
                    /* Fire onTextInput after onKeyDown when the key
                       produced a printable ASCII character. ASCII only
                       for v1; non-ASCII unicode is dropped. */
                    Uint16 uni = event.key.keysym.unicode;
                    if (uni >= 0x20 && uni <= 0x7E) {
                        char buf[2] = { (char)uni, '\0' };
                        scriptCallTextInput(&script, buf);
                    }
                }
                if (event.type == SDL_KEYUP) {
                    const char *name = SDL_GetKeyName(event.key.keysym.sym);
                    scriptCallKeyUp(&script, name ? name : "");
                }
                if (event.type == SDL_MOUSEMOTION) {
                    float vx = 0.0f, vy = 0.0f;
                    uiMouseToVirtual(&ui, event.motion.x, event.motion.y, &vx, &vy);
                    float scale = (ui.virtualH > 0)
                                ? (ui.virtualH / (float)SCREEN_H) : 1.0f;
                    float dvx = event.motion.xrel * scale;
                    float dvy = event.motion.yrel * scale;
                    scriptCallMouseMove(&script, vx, vy, dvx, dvy);
                }
                if (event.type == SDL_MOUSEBUTTONDOWN) {
                    float vx = 0.0f, vy = 0.0f;
                    uiMouseToVirtual(&ui, event.button.x, event.button.y, &vx, &vy);
                    scriptCallMouseDown(&script, vx, vy, event.button.button);
                }
                if (event.type == SDL_MOUSEBUTTONUP) {
                    float vx = 0.0f, vy = 0.0f;
                    uiMouseToVirtual(&ui, event.button.x, event.button.y, &vx, &vy);
                    scriptCallMouseUp(&script, vx, vy, event.button.button);
                }
            }

            scriptCallUpdate(&script, dt);

            glClearColor(0, 0, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            uiBegin(&ui);
            scriptCallRender(&script);
            uiDrawMessage(&ui);
            uiEnd(&ui);
        } else {
        /* ---- MODE_GAME: gameplay event handling + simulation + render ---- */
        game.fpsAccum += dt;
        game.fpsFrames++;
        if (game.fpsAccum >= 0.5f) {
            game.fpsDisplay = (int)(game.fpsFrames / game.fpsAccum + 0.5f);
            game.fpsAccum = 0.0f;
            game.fpsFrames = 0;
        }

        conUpdate(&con, dt);

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                app.running = 0;
                break;
            }
            if (event.type == SDL_KEYDOWN) {
                SDLKey sym = event.key.keysym.sym;
                Uint16 uni = event.key.keysym.unicode;

                /* Backtick: toggle the console, regardless of prior state.
                   Also flips mouse grab: open → free cursor so typing
                   without mouselook is comfortable; close → re-grab for
                   FPS controls. Don't let the backtick reach conText
                   (would otherwise be appended to the command buffer). */
                if (sym == SDLK_BACKQUOTE) {
                    conToggle(&con);
                    if (con.open) {
                        SDL_WM_GrabInput(SDL_GRAB_OFF);
                        SDL_ShowCursor(SDL_ENABLE);
                    } else {
                        SDL_WM_GrabInput(SDL_GRAB_ON);
                        SDL_ShowCursor(SDL_DISABLE);
                    }
                    continue;
                }

                /* Esc always goes to the menu — same keybinding whether or
                   not the console is open. Close the console on the way
                   so the next menu visit is clean. */
                if (sym == SDLK_ESCAPE) {
                    conClose(&con);
                    app.mode = MODE_MENU;
                    appEnterMenu(&app);
                    SDL_WM_GrabInput(SDL_GRAB_OFF);
                    SDL_ShowCursor(SDL_ENABLE);
                    break;
                }

                /* Console capturing keyboard — route the key + printable
                   unicode to it instead of the game-action handlers. */
                if (conCapturesInput(&con)) {
                    int kr = conKey(&con, sym);
                    if (kr == CON_KEY_EXEC) {
                        conExecute(&con, &script);
                        con.cmdLen = 0;
                        con.cmd[0] = '\0';
                    } else if (kr == CON_KEY_NONE) {
                        /* Printable character → append to the command. */
                        if (uni >= 0x20 && uni <= 0x7E) conText(&con, uni);
                    }
                    continue;
                }

                if (event.key.keysym.sym == SDLK_f) {
                    game.flashlightOn = !game.flashlightOn;
                    conLogf("Flashlight: %s\n", game.flashlightOn ? "ON" : "OFF");
                }
                if (event.key.keysym.sym == SDLK_b) {
                    game.debugColliders = !game.debugColliders;
                    conLogf("Collider wireframes: %s\n", game.debugColliders ? "ON" : "OFF");
                }
                if (event.key.keysym.sym == SDLK_n) {
                    game.debugNav = !game.debugNav;
                    conLogf("Nav graph: %s (%d node%s)\n",
                           game.debugNav ? "ON" : "OFF",
                           game.nav.numNodes, game.nav.numNodes == 1 ? "" : "s");
                    /* One-shot A* test from player to the last waypoint so the
                       pathfinder has been exercised before any AI uses it. */
                    if (game.debugNav && game.nav.numNodes >= 2) {
                        Vec3 from = physGetPlayerPos(&game.phys);
                        Vec3 to = game.nav.nodes[game.nav.numNodes - 1];
                        int path[NAV_MAX_NODES];
                        int len = navFindPath(&game.nav, &game.phys, from, to,
                                              path, NAV_MAX_NODES);
                        conLogf("nav: test path (player -> node %d): %d step(s)",
                               game.nav.numNodes - 1, len);
                        for (int i = 0; i < len; i++) conLogf(" %d", path[i]);
                        conLogf("\n");
                    }
                }
                if (event.key.keysym.sym == SDLK_p) {
                    Vec3 p = physGetPlayerPos(&game.phys);
                    conLogf("player pos: X=%.3f Y=%.3f Z=%.3f  yaw=%.1f pitch=%.1f\n",
                           p.x, p.y, p.z, game.yaw, game.pitch);
                }
                if (event.key.keysym.sym == SDLK_SPACE) {
                    if (game.phys.character->onGround()) {
                        game.phys.character->jump();
                        sndPlay(&snd, sndLibPick(&sndLib, "jump"));
                    }
                }
            }
            if (event.type == SDL_MOUSEBUTTONDOWN) {
                if (event.button.button == SDL_BUTTON_LEFT &&
                    !conCapturesInput(&con)) {
                    game.gunFlashTimer = 4;
                    sndPlay(&snd, sndLibPick(&sndLib, "fire"));
                }
            }
            if (event.type == SDL_MOUSEMOTION && !conCapturesInput(&con)) {
                game.yaw   -= event.motion.xrel * 0.15f;
                game.pitch -= event.motion.yrel * 0.15f;
                if (game.pitch > 89.0f) game.pitch = 89.0f;
                if (game.pitch < -89.0f) game.pitch = -89.0f;
            }
        }

        /* WASD movement */
        Uint8 *keys = SDL_GetKeyState(NULL);
        float forwardX = -sinf(game.yaw * (float)M_PI / 180.0f);
        float forwardZ = -cosf(game.yaw * (float)M_PI / 180.0f);
        float rightX = cosf(game.yaw * (float)M_PI / 180.0f);
        float rightZ = -sinf(game.yaw * (float)M_PI / 180.0f);

        /* Desired (wish) velocity in m/s from input. While the console is
           open all player input (keyboard + mouse) is paused so typing
           doesn't double as gameplay; simulation (physics, entities,
           doors) continues to tick. */
        float wishX = 0, wishZ = 0;
        if (!conCapturesInput(&con)) {
            if (keys[SDLK_w]) { wishX += forwardX; wishZ += forwardZ; }
            if (keys[SDLK_s]) { wishX -= forwardX; wishZ -= forwardZ; }
            if (keys[SDLK_a]) { wishX -= rightX;   wishZ -= rightZ; }
            if (keys[SDLK_d]) { wishX += rightX;   wishZ += rightZ; }
        }
        float wishLen = sqrtf(wishX * wishX + wishZ * wishZ);
        if (wishLen > 0.001f) {
            wishX = (wishX / wishLen) * moveSpeed;
            wishZ = (wishZ / wishLen) * moveSpeed;
        }

        /* HL1-style acceleration + sliding. Accel brings velocity toward the
           wish velocity; friction applies when no input. Higher accel = more
           responsive; higher friction = shorter slide. Numbers roughly match
           GoldSrc defaults scaled to our 6 m/s max. */
        const float ACCEL    = 10.0f;   /* m/s²-ish pull toward wish velocity */
        const float FRICTION = 20.0f;   /* m/s²-ish slowdown when no input    */
        if (wishLen > 0.001f) {
            game.velX += (wishX - game.velX) * ACCEL * dt;
            game.velZ += (wishZ - game.velZ) * ACCEL * dt;
        } else {
            float speed = sqrtf(game.velX * game.velX + game.velZ * game.velZ);
            if (speed > 0.001f) {
                float drop = FRICTION * dt;
                float newSpeed = speed - drop;
                if (newSpeed < 0) newSpeed = 0;
                game.velX *= newSpeed / speed;
                game.velZ *= newSpeed / speed;
            } else {
                game.velX = 0; game.velZ = 0;
            }
        }
        int isMoving = (game.velX * game.velX + game.velZ * game.velZ) > 0.04f;

        /* Convert velocity (m/s) to per-substep displacement for Bullet. */
        const float perStep = 1.0f / 120.0f;
        game.phys.character->setWalkDirection(
            btVector3(game.velX * perStep, 0, game.velZ * perStep));

        if (isMoving && game.phys.character->onGround()) {
            game.footstepTimer -= (int)(dt * 1000);
            if (game.footstepTimer <= 0) {
                sndPlay(&snd, sndLibPick(&sndLib, "steps"));
                game.footstepTimer = 400;
            }
        } else {
            game.footstepTimer = 0;
        }

        /* Platforms move before physStep so the kinematic collider position
           and (Step 6) the carried-rider ghost both land in the same tick. */
        updatePlatforms(game.entities, &game.paths, &game.phys, dt);

        physStep(&game.phys, dt);

        Vec3 ppos = physGetPlayerPos(&game.phys);
        float px = ppos.x, py = ppos.y, pz = ppos.z;
        /* Capsule center is 0.875m above the floor when grounded (halfHeight
           0.525 + radius 0.35), so +0.755 puts the camera at 1.63m — realistic
           eye height for a 1.75m-tall person. */
        Vec3 eye = { px, py + 0.765f, pz };
        float eyeY = eye.y;

        Vec3 look = { px + forwardX,
                      eyeY + sinf(game.pitch * (float)M_PI / 180.0f),
                      pz + forwardZ };
        float lookX = look.x, lookY = look.y, lookZ = look.z;

        /* Push camera pose to OpenAL so positioned sounds (sndPlayAt /
           soundPlay with coords) pan and attenuate correctly. Up vector
           is world Y; the strafe roll above is a visual flourish only,
           we don't roll the listener with it. */
        {
            Vec3 lfwd = { lookX - px, lookY - eyeY, lookZ - pz };
            Vec3 lup  = { 0.0f, 1.0f, 0.0f };
            sndUpdateListener(&snd, eye, lfwd, lup);
        }

        if (game.gunFlashTimer > 0) game.gunFlashTimer--;

        /* ---- Render ---- */
        glClearColor(0.4f, 0.6f, 0.8f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        /* Camera roll on strafe (HL1 v_iroll_angle): the velocity component
           along the right vector produces a bank angle, clamped to MAX_ROLL.
           Positive strafe (moving right) tilts head right, which in view
           space is a CCW rotation around the view's Z axis. Passed into
           renderWorld, which applies it before the look-at. */
        float roll;
        {
            float strafeVel = game.velX * rightX + game.velZ * rightZ;
            const float ROLL_SPEED = 6.0f;    /* vel at which we hit max roll */
            const float MAX_ROLL   = 2.0f;    /* degrees */
            roll = strafeVel / ROLL_SPEED * MAX_ROLL;
            if (roll >  MAX_ROLL) roll =  MAX_ROLL;
            if (roll < -MAX_ROLL) roll = -MAX_ROLL;
        }

        /* Flashlight (HL1-style: modify lightmap pixels on CPU, re-upload).
           Simulation — updates the level's dynamic lightmap texture, so it
           must run before renderWorld draws the level. */
        if (game.flashlightOn && game.hasDynLm) {
            float dirX = lookX - px;
            float dirY = lookY - eyeY;
            float dirZ = lookZ - pz;
            float dirLen = sqrtf(dirX*dirX + dirY*dirY + dirZ*dirZ);
            if (dirLen > 0.0001f) { dirX /= dirLen; dirY /= dirLen; dirZ /= dirLen; }

            Vec3 dir = { dirX, dirY, dirZ };
            Vec3 hit;
            if (physRaycast(&game.phys, eye, dir, 30.0f, &hit)) {
                Vec3 col = { 1.0f, 0.95f, 0.8f };
                dynLmUpdate(&game.dynLm, hit, 3.0f, 1.0f, col);
            } else {
                dynLmRestore(&game.dynLm);
            }
        } else if (game.hasDynLm) {
            dynLmRestore(&game.dynLm);
        }

        /* Entity simulation (movement, doors) — step before drawing. */
        entUpdate(game.entities, px, py, pz, dt);
        updateDoors(game.entities, &game.phys, dt);

        /* Draw the lit level + entities through the shared world-render path
           (same code the level editor will call). Overlays below are drawn on
           top by the game only. */
        Camera cam;
        cam.eye = eye;
        cam.at  = look;
        cam.rollDeg = roll;
        renderWorld(&game, cam);

        if (game.debugColliders) {
            physDebugDrawColliders(&game.phys);
            pathDebugRender(&game.paths, game.entities);
        }
        if (game.debugNav) navDebugRender(&game.nav);

        renderGun(game.gunFlashTimer);
        renderCrosshair();

        /* HUD — placeholder values until player/weapon state exists.
           Coordinates are virtual (1080-tall canvas, origin at center). */
        uiBegin(&ui);
        {
            const float pad = 15.0f;
            const float barW = 200.0f, barH = 22.0f;
            const float halfW = uiGetWidth(&ui)  * 0.5f;
            const float halfH = uiGetHeight(&ui) * 0.5f;
            UiColor white = uiRgb(1, 1, 1);
            UiColor red   = uiRgb(0.85f, 0.2f, 0.2f);
            UiColor amber = uiRgb(1.0f, 1.0f, 0.2f);

            /* HUD text scales are now in the new uiText convention:
               scale * font.lineHeight = on-screen line height in vpx.
               Default font is orbitron_small (lineHeight = 28). Targets
               match the pre-SOOB-Core sizes (20 / 28 / 32 / 16 vpx). */

            /* Health: bottom-left */
            uiText(&ui, -halfW + pad, halfH - pad - barH - 17,
                   white, "HEALTH", 20.0f / 28.0f);             /* ~20 vpx */
            uiBar(uiRectMake(-halfW + pad, halfH - pad - barH, barW, barH),
                  0.87f, red);
            uiText(&ui, -halfW + pad + barW + 10, halfH - pad - barH + 2,
                   white, "87", 28.0f / 28.0f);                 /* ~28 vpx */

            /* Ammo: bottom-right */
            char ammo[32];
            snprintf(ammo, sizeof(ammo), "%d / %d", 24, 72);
            uiText(&ui, halfW - pad, halfH - pad - 20, amber, ammo,
                   32.0f / 28.0f, UI_ALIGN_TOP | UI_ALIGN_RIGHT);   /* ~32 vpx */

            /* FPS: top-right */
            char fps[32];
            snprintf(fps, sizeof(fps), "%d FPS", game.fpsDisplay);
            uiText(&ui, halfW - pad, -halfH + pad, white, fps,
                   16.0f / 28.0f, UI_ALIGN_TOP | UI_ALIGN_RIGHT);   /* ~16 vpx */

            /* Transient script-driven message (uiShowMessage from Lua). */
            uiDrawMessage(&ui);

            /* Dev console — drawn last so it overlays the HUD. Resolve the
               tiled background texture up here so console.h has no asset
               registry / texture-cache dependency. */
            const char *cbgPath = assetRegFindTexture(&assetReg, "dialog_bg");
            GLuint conBgTex = cbgPath
                ? texCacheGet(&app.menuTex, cbgPath, GL_REPEAT) : 0;
            conRender(&con, &ui, conBgTex);
        }
        uiEnd(&ui);
        }  /* end MODE_GAME branch */

        /* Process side-effect actions surfaced by the menu or game loop. */
        switch (app.pendingAction) {
            case PENDING_NEW_GAME:
                /* Paint a single "LOADING" frame before gameInit blocks the
                   thread, so the window doesn't sit on a stale menu frame
                   (or black) while the level and physics come up. */
                drawLoadingScreen(&app);
                if (gameInited) {
                    gameFree(&game, &script);
                    gameInited = 0;
                    app.game = NULL;
                }
                if (!gameInit(&game, "assets/levels/test_level.obj",
                              "assets/levels/test_level.ent", &script, &assetReg)) {
                    conLogf("gameInit failed\n");
                    app.running = 0;
                } else {
                    gameInited = 1;
                    app.game = &game;
                    app.mode = MODE_GAME;
                    /* Lua scene stack stays intact across the mode flip —
                       the main menu sits at the bottom throughout. */
                    SDL_WM_GrabInput(SDL_GRAB_ON);
                    SDL_ShowCursor(SDL_DISABLE);
                }
                break;
            case PENDING_CONTINUE:
                if (gameInited) {
                    app.mode = MODE_GAME;
                    SDL_WM_GrabInput(SDL_GRAB_ON);
                    SDL_ShowCursor(SDL_DISABLE);
                }
                break;
            case PENDING_QUIT:
                app.running = 0;
                break;
        }
        app.pendingAction = PENDING_NONE;

        SDL_GL_SwapBuffers();
        SDL_Delay(1);
    }

    /* Cleanup */
    if (gameInited) gameFree(&game, &script);
    appShutdown(&app);
    scriptShutdown(&script);
    texBlurFree(&blurCache);
    uiShutdown(&ui);

    musicShutdown(&mus);
    sndLibShutdown(&sndLib);
    sndShutdown(&snd);
    SDL_Quit();
    return 0;
}
