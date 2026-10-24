#ifndef SCRIPT_EXT_H
#define SCRIPT_EXT_H

/*
 * SOOB-Engine-specific Lua bindings layered on top of the shared SOOB-Core
 * script.h. The engine doesn't know about the entity system, so any
 * binding that needs an EntityList lives here.
 *
 * Currently:
 *   entActivate(target)  -> dispatch by entity name OR group, no-op if
 *                            no game session is active.
 *
 * Include after script.h (for ScriptSystem) and entity.h (for EntityList).
 *
 * The active EntityList pointer is file-static. gameInit / gameFree
 * set and clear it via scriptExtSetEntities(), mirroring the lifecycle
 * the old ScriptSystem::entities rebind used to do.
 */

static EntityList *g_scriptExt_entities = NULL;

/* entActivate(target) — matches by entity name OR group, cascades
   switch targets. Logs a warning (rather than crashing) if called while
   no game is active — possible if a menu-side script gets the binding
   wrong. */
static int scrEntActivateExt(lua_State *L)
{
    const char *target = luaL_checkstring(L, 1);
    if (g_scriptExt_entities) {
        entActivate(g_scriptExt_entities, target);
    } else {
        conLogf("entActivate('%s'): no active game session — ignored\n", target);
    }
    return 0;
}

/* Register SOOB-Engine's extra bindings on top of an already-initialized
   ScriptSystem. Call once after scriptInit. */
static void scriptExtRegister(ScriptSystem *s)
{
    lua_register(s->L, "entActivate", scrEntActivateExt);
}

/* Execute the console's command buffer as a Lua chunk. Used by console.h
   on Enter — bridges SOOB-Engine's dev console to the engine's Lua runtime.
   Find5 doesn't have a dev console so this lives here, not in SOOB-Core.
   scrTraceback comes from script.h (same TU; static functions are visible
   between headers that get textually included together). */
static void conExecute(Console *c, ScriptSystem *s)
{
    conLogf("> %s\n", c->cmd);
    lua_State *L = s->L;
    lua_pushcfunction(L, scrTraceback);
    int tbidx = lua_gettop(L);
    if (luaL_loadstring(L, c->cmd) != 0) {
        conLogf("%s\n", lua_tostring(L, -1));
        lua_pop(L, 2);   /* error + traceback */
        return;
    }
    if (lua_pcall(L, 0, 0, tbidx) != 0) {
        conLogf("%s\n", lua_tostring(L, -1));
        lua_pop(L, 2);
        return;
    }
    lua_pop(L, 1);       /* traceback */
}

/* Set or clear the EntityList that entActivate dispatches into. Called
   by gameInit (with the new session's list) and gameFree (with NULL). */
static void scriptExtSetEntities(EntityList *el)
{
    g_scriptExt_entities = el;
}

#endif /* SCRIPT_EXT_H */
