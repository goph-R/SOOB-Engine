#ifndef APP_EXT_H
#define APP_EXT_H

/*
 * SOOB-Engine-specific Lua bindings layered on top of menu.h's AppState.
 * Splits from script_ext.h because these bindings need the full
 * AppState definition, which lives in menu.h — included AFTER game.h
 * (which in turn references script_ext.h's scriptExtSetEntities).
 *
 * Bindings:
 *   appNewGame()        -> sets pendingAction = PENDING_NEW_GAME
 *   appContinue()        -> sets pendingAction = PENDING_CONTINUE
 *                            (no-op if no game session yet)
 *   appQuit()            -> sets pendingAction = PENDING_QUIT
 *   appHasGame() -> bool  whether a game session has been created
 *                          and is still alive (drives CONTINUE enable)
 *   appEnterMenu()      -> flips mode back to MODE_MENU (Lua side
 *                            usually drives this implicitly by Esc
 *                            from within the game; provided for
 *                            symmetry with the C transition)
 *
 * Include AFTER menu.h. main.cpp wires the AppState pointer via
 * appExtSetApp(&app) after appInit completes.
 */

static AppState *g_appExt_app = NULL;

static int scrAppNewGame(lua_State *L)
{
    (void)L;
    if (g_appExt_app) g_appExt_app->pendingAction = PENDING_NEW_GAME;
    return 0;
}

static int scrAppContinue(lua_State *L)
{
    (void)L;
    if (g_appExt_app && g_appExt_app->game) {
        g_appExt_app->pendingAction = PENDING_CONTINUE;
    } else if (g_appExt_app) {
        conLogf("appContinue: no game session to resume — ignored\n");
    }
    return 0;
}

static int scrAppQuit(lua_State *L)
{
    (void)L;
    if (g_appExt_app) g_appExt_app->pendingAction = PENDING_QUIT;
    return 0;
}

static int scrAppHasGame(lua_State *L)
{
    lua_pushboolean(L, g_appExt_app && g_appExt_app->game != NULL);
    return 1;
}

static int scrAppEnterMenu(lua_State *L)
{
    (void)L;
    if (g_appExt_app) g_appExt_app->mode = MODE_MENU;
    return 0;
}

/* Register the app bindings on the ScriptSystem. Call once after
   scriptInit (and after script_ext.h's scriptExtRegister). */
static void appExtRegister(ScriptSystem *s)
{
    lua_register(s->L, "appNewGame",  scrAppNewGame);
    lua_register(s->L, "appContinue",  scrAppContinue);
    lua_register(s->L, "appQuit",      scrAppQuit);
    lua_register(s->L, "appHasGame",  scrAppHasGame);
    lua_register(s->L, "appEnterMenu",scrAppEnterMenu);
}

/* Set / clear the AppState pointer the bindings reach into.
   main.cpp calls appExtSetApp(&app) once after appInit. */
static void appExtSetApp(AppState *app)
{
    g_appExt_app = app;
}

#endif /* APP_EXT_H */
