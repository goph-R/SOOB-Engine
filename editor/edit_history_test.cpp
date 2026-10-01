/*
 * edit_history_test.cpp — headless round-trip for the unified undo (EM1).
 * Verifies the COMPACT entity snapshot: transform + union restore on undo/redo,
 * while the heavy per-entity mesh/anim fields are left untouched.
 *
 * entity.h pulls in texture.h/iqm.h, which reference GL symbols (never called
 * here), so this one links against GL — unlike the pure-mesh tests:
 *   g++ -I. -I../SOOB-Core -I../SOOB-Core/vendor/fltk-1.3/FL editor/edit_history_test.cpp -lGL -o /tmp/eht && /tmp/eht
 */
#include <cstdio>
#include <cstdarg>
#include <cstring>

static void conLogf(const char *f, ...) { va_list a; va_start(a, f); vprintf(f, a); va_end(a); }

#include "edit_history.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

int main(void)
{
    EntityList *l = (EntityList *)malloc(sizeof(EntityList));
    entListInit(l);

    /* a door (exercises the type-specific union) + a waypoint (transform only) */
    Entity *d = &l->entities[l->count++];
    memset(d, 0, sizeof(*d)); d->active = 1; d->type = ENT_DOOR;
    strcpy(d->name, "door1"); strcpy(d->group, "props");
    d->posX = 2; d->posY = 0; d->posZ = -3; d->rotY = 90; d->scale = 1;
    d->hasMesh = 1; d->door.amount = 90; d->door.motion = 1; d->door.speed = 120;

    Entity *w = &l->entities[l->count++];
    memset(w, 0, sizeof(*w)); w->active = 1; w->type = ENT_WAYPOINT;
    strcpy(w->name, "wp"); w->posX = 5; w->posY = 1; w->posZ = 5; w->scale = 1;

    DocHistory h; docHistInit(&h);
    docPushEnts(&h, l);                          /* snapshot before mutating */

    /* move the door, tweak a union field, and poison a "heavy" field */
    d->posX = 9; d->posY = 4; d->door.amount = 45; d->hasMesh = 42;

    EditMesh dummy; editMeshInit(&dummy);        /* mesh side is inert here */
    int k = docUndo(&h, &dummy, l);
    CHECK(k == DOC_ENTS);
    CHECK(d->posX == 2 && d->posY == 0 && d->posZ == -3);   /* transform restored */
    CHECK(d->door.amount == 90);                            /* union restored */
    CHECK(d->hasMesh == 42);            /* heavy field NOT restored (left intact) */
    CHECK(l->count == 2);
    CHECK(strcmp(w->name, "wp") == 0 && w->type == ENT_WAYPOINT);

    int k2 = docRedo(&h, &dummy, l);
    CHECK(k2 == DOC_ENTS);
    CHECK(d->posX == 9 && d->door.amount == 45);            /* mutation reapplied */

    /* interleave a mesh push: one Ctrl+Z must revert the mesh, not the entity */
    docPushMesh(&h, &dummy);
    int k3 = docUndo(&h, &dummy, l);
    CHECK(k3 == DOC_MESH);
    CHECK(d->posX == 9);                                    /* entity untouched */

    editMeshFree(&dummy);
    docHistFree(&h);
    free(l);
    printf(fails ? "\n%d FAILURE(S)\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
