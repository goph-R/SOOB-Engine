-- Asset manifest for SOOB-Engine.
--
-- Lists logical name -> file path for every game asset. Loaded at
-- startup by scriptLoadAssets (script.h), which walks the tables and
-- populates the engine-side registries (SoundLibrary for `sounds`,
-- AssetRegistry for `models` and `textures`).
--
-- .ent files reference models/textures by the short names below
-- (mesh=office_desk, tex=wood1, iqm=mrfixit). Unknown names fall
-- through as raw paths so one-offs still work without registering.
--
-- Name convention: short, lowercase, matching what game code / scripts
-- will pass to soundPlay("...") etc. File paths are relative to the repo
-- root (same convention as every other asset in the engine).

return {
    sounds = {
        -- Single sounds: name = "path"
        fire  = "assets/sounds/fire.wav",
        jump  = "assets/sounds/jump.wav",
        -- Random-pick groups: name = { "path1", "path2", ... }. soundPlay
        -- (or sndLibPick from C) picks a uniformly-random non-repeating
        -- variant each call. Up to SND_MAX_VARIANTS (4) per group.
        steps = {
            "assets/sounds/step1.wav",
            -- "assets/sounds/step2.wav",
            -- "assets/sounds/step3.wav",
        },
    },

    -- Streaming Ogg Vorbis tracks for musicPlay(name [, fade [, loop]]).
    -- Unlike `sounds`, music isn't preloaded — files are opened lazily on
    -- the first musicPlay. "title" is auto-played by the menu on boot;
    -- name additional tracks (ambient, combat, etc.) and trigger them
    -- from onStart or scripted events. Drop .ogg files in assets/music/.
    music = {
        title    = "assets/music/title.ogg",
        arriving = "assets/music/arriving.ogg",
        -- combat  = "assets/music/combat_loop.ogg",
    },

    models = {
        mrfixit          = "assets/models/mrfixit.iqm",
        office_desk      = "assets/models/office-desk.obj",
        crt_monitor      = "assets/models/crt-monitor.obj",
        keyboard         = "assets/models/keyboard.obj",
        pc_tower         = "assets/models/midi-tower-pc-case.obj",
        office_chair     = "assets/models/office-chair.obj",
        door             = "assets/models/door.obj",
        portrait_frame   = "assets/models/portrait-frame.obj",
        portrait_photo1  = "assets/models/portrait-photo1.obj",
        portrait_photo2  = "assets/models/portrait-photo2.obj",
        landscape_frame  = "assets/models/landscape-frame.obj",
        landscape_photo1 = "assets/models/landscape-photo1.obj",
        landscape_photo2 = "assets/models/landscape-photo2.obj",
        -- Enemies (see docs/min-enemy-assets.md).
        zombie         = "assets/models/zombie.iqm",
    },

    textures = {
        office      = "assets/textures/office.png",
        wood1       = "assets/textures/wood1.png",
        wood2       = "assets/textures/wood2.png",
        portraits1  = "assets/textures/portraits1.png",
        landscapes1 = "assets/textures/landscapes1.png",
        -- Menu/UI textures.
        menu_bg    = "assets/textures/menu_bg.png",
        dialog_bg  = "assets/textures/dialog_bg.png",
        logo       = "assets/textures/logo.png",
        loading_bg = "assets/textures/loading_bg.png",
    },

    -- BMFont (AngelCode) bitmap fonts. Each entry points at a .fnt text file;
    -- the .fnt carries the atlas filename (a 32-bit RGBA PNG in the same directory)
    -- and per-glyph metrics. "default" is the font used when a uiText call
    -- passes no explicit font name — alias it to whichever font you want as
    -- the HUD baseline.
    --
    -- Loading the same .fnt under multiple logical names (e.g. button_font /
    -- orbitron_small) is cheap: uiFontLoad deduplicates by path and shares
    -- the atlas texture + glyph table between entries.
    fonts = {
        default         = "assets/fonts/orbitron_small.fnt",
        orbitron        = "assets/fonts/orbitron.fnt",
        orbitron_small  = "assets/fonts/orbitron_small.fnt",
        -- Menu-system aliases (same .fnt files, different logical names).
        button_font     = "assets/fonts/orbitron_small.fnt",
        dialog_title    = "assets/fonts/orbitron.fnt",
        menu_title_font = "assets/fonts/orbitron.fnt",
    },

    -- Regions are sub-rectangles of textures, addressed by name from
    -- drawRegion / drawBg. Full-texture regions are (0, 0, full_w,
    -- full_h). The Lua menu (scripts/menu.lua) draws these by name.
    regions = {
        menu_bg    = { tex = "menu_bg",    x = 0, y = 0, w = 256, h = 128 },
        logo       = { tex = "logo",       x = 0, y = 0, w = 256, h = 64  },
        dialog_bg  = { tex = "dialog_bg",  x = 0, y = 0, w = 64,  h = 64  },
        loading_bg = { tex = "loading_bg", x = 0, y = 0, w = 256, h = 128 },
    },

    levels = {
        level1 = { obj = "assets/levels/test_level.obj", ent = "assets/levels/test_level.ent" },
        -- level2 = { obj = "...", ent = "..." },
    },
}
