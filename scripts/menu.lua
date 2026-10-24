-- Main-menu / options / confirm-dialog scenes for SOOB-Engine.
--
-- Each constructor returns a "scene" table (see engine/scene.lua) whose
-- widgets live inside a `root` panel. engine.scene's dispatchers auto-
-- forward update / mouse / key / textInput into root, so each scene
-- only defines what's actually scene-specific (enter to build widgets,
-- render for backdrop / title art, keyDown for Esc handling).

local scene      = require "engine.scene"
local widget     = require "engine.widget"
local transition = require "engine.transition"
local audio      = require "audio"

-- Tunables for dialog/options slide-in. Same duration across all so
-- the menu's modal layer has a consistent rhythm.
local DLG_SLIDE_DUR    = 0.3
local OPT_SLIDE_DUR    = 0.3

local M = {}

-- ---- Layout constants ---------------------------------------------------
-- Mirrors menu.h's old MENU_* constants. Distances are in virtual-canvas
-- units; SOOB-Engine's canvas is 540 tall (set in main.cpp before including
-- ui.h).
local PAD       = 30
local BTN_W     = 190
local BTN_H     = 36
local BTN_GAP   = 7
local LOGO_H    = 48
local LOGO_W    = LOGO_H * 4    -- logo asset is 256x64, 4:1 aspect
local DLG_W     = 360
local DLG_H     = 170
local DLG_BTN_W = 110
local DLG_BTN_H = 36
local DLG_PAD   = 20

-- Text scales — multipliers of the font's native lineHeight.
-- Source fonts (per assets.lua aliases): orbitron lineHeight=40,
-- orbitron_small lineHeight=28.
local SCALE_BUTTON       = 0.85   -- button_font  (target ~24 vpx)
local SCALE_DIALOG_MSG   = 0.70   -- button_font  (target ~20 vpx)
local SCALE_DIALOG_TITLE = 0.90   -- dialog_title (target ~36 vpx)
local SCALE_MENU_TITLE   = 1.00   -- menu_title_font (target ~40 vpx)

-- Shared button styling — flat-color backgrounds keyed by state. Once
-- button art lands in assets.lua, swap to bgUp / bgDown / bgDisabled
-- (region names with slice metadata) on the same widget.button calls.
local BTN_BG_COLOR = {
    up       = { 0.14, 0.14, 0.22, 0.75 },
    focused  = { 0.55, 0.25, 0.85, 0.85 },
    down     = { 0.55, 0.25, 0.85, 1.00 },
    disabled = { 0.10, 0.10, 0.14, 0.75 },
}
local BTN_FG_COLOR = { 0.90, 0.90, 0.95, 1.0 }

-- One factory for every button in the menu — same look, just different
-- label / position / action.
local function makeMenuButton(x, y, w, h, label, onClick)
    return widget.button({
        x = x, y = y, width = w, height = h,
        text       = label,
        font       = "button_font",
        textScale = SCALE_BUTTON,
        textAlign = ALIGN_MIDDLE + ALIGN_LEFT,
        textColor = BTN_FG_COLOR,
        bgColor   = BTN_BG_COLOR,
        onClick   = onClick,
    })
end

-- ---- Main menu ---------------------------------------------------------

function M.mainMenu()
    local mm = { root = widget.panel({ x = 0, y = 0 }) }

    function mm:enter()
        local vw, vh = viewSize()
        local hw, hh = vw * 0.5, vh * 0.5

        -- Logo at top-left.
        self.logoX = -hw + PAD
        self.logoY = -hh + PAD

        -- Button column under the logo.
        local x = -hw + PAD
        local y = -hh + PAD + LOGO_H + PAD
        local row = function(i) return y + (BTN_H + BTN_GAP) * i end

        -- CONTINUE — disabled flag set BEFORE :add so the panel's auto-
        -- focus skips it (isFocusable rejects disabled). The next
        -- focusable child (NEW GAME) then claims focus, matching the
        -- old widget.focusNext(nil) behaviour.
        local continue = makeMenuButton(x, row(0), BTN_W, BTN_H, "CONTINUE",
            function() appContinue() end)
        continue.disabled = not appHasGame()
        self.root:add(continue)

        self.root:add(makeMenuButton(x, row(1), BTN_W, BTN_H, "NEW GAME",
            function()
                if appHasGame() then
                    scene.push(M.confirmDialog(
                        "NEW GAME",
                        "Start a new game? Progress will be lost.",
                        appNewGame),
                        transition.slide("bottom", DLG_SLIDE_DUR))
                else
                    appNewGame()
                end
            end))

        self.root:add(makeMenuButton(x, row(2), BTN_W, BTN_H, "OPTIONS",
            function()
                scene.push(M.options(),
                           transition.slide("top", OPT_SLIDE_DUR))
            end))

        self.root:add(makeMenuButton(x, row(3), BTN_W, BTN_H, "EXIT",
            function()
                scene.push(M.confirmDialog("EXIT", "Exit to system?", appQuit),
                           transition.slide("bottom", DLG_SLIDE_DUR))
            end))

        -- Title music starts on first menu entry; later re-enters
        -- crossfade smoothly. audio.play stays silent while music_on
        -- is false but remembers the request so toggling Music on later
        -- resumes "title".
        audio.play("title", 1.0, true)
    end

    -- CONTINUE's enabled flag tracks appHasGame() — that becomes true
    -- after the first New Game and stays true through subsequent
    -- Esc → menu → Continue roundtrips. Sync once per frame from render;
    -- if focus was sitting on CONTINUE and it just got disabled, slide
    -- focus to the next focusable widget so the keyboard isn't stuck.
    function mm:_sync_continue()
        local cont = self.root.children[1]
        local nowDisabled = not appHasGame()
        if cont.disabled == nowDisabled then return end
        cont.disabled = nowDisabled
        if nowDisabled and self.root.focusedChild == cont then
            local nextW = widget.focusNext(self.root.children, cont)
            if nextW and nextW ~= cont then
                cont.focused = false
                self.root.focusedChild = nextW
                nextW.focused = true
            end
        end
    end

    function mm:render()
        self:_sync_continue()
        drawBg("menu_bg")
        drawRegion("logo", self.logoX, self.logoY, {
            scaleX = LOGO_W / 256.0,
            scaleY = LOGO_H / 64.0,
        })
        self.root:draw()
    end

    return mm
end

-- ---- Options -----------------------------------------------------------
--
-- Sample settings wired via optSet / optGet + the engine's
-- musicVolume binding. Used as the dogfood scene for Checkbox + Slider
-- + Label + LineEdit.
--
-- - "Music" checkbox toggles persisted `musicOn` and mutes/restores
--   the music volume immediately.
-- - "Master volume" slider sets persisted `musicVol` and applies it
--   live whenever Music is on.
-- - "Player name" LineEdit persists the typed text via optSet.
--
-- Options are saved to soob.dat when the user presses BACK / Esc /
-- Backspace; onChange runs only in-memory + applies the live audio
-- effect so dragging the slider doesn't thrash the disk.

function M.options()
    local opt = {
        transparent = true,                -- main menu renders behind us
        root        = widget.panel({ x = 0, y = 0 }),
    }
    local vw, vh = viewSize()
    opt.vw, opt.vh = vw, vh
    opt.titleY = -vh * 0.5 + PAD

    -- Persisted defaults — sane on first run.
    local musicOn    = optGet("music_on",    true)
    local musicVol   = optGet("music_vol",   0.7)
    local playerName = optGet("player_name", "")

    -- Layout: settings column starts below the title, centred.
    local COL_W = 320
    local COL_X = -COL_W * 0.5
    local rowY = opt.titleY + 60

    local musicSlider

    local musicCheck = widget.checkbox{
        x = COL_X, y = rowY, width = COL_W, height = BTN_H,
        text       = "Music",
        font       = "button_font",
        textScale = SCALE_BUTTON,
        color      = BTN_FG_COLOR,
        value      = musicOn,
        onChange = function(self, v) audio.setEnabled(v) end,
    }
    rowY = rowY + BTN_H + BTN_GAP

    local volLabel = widget.label{
        x = COL_X, y = rowY, width = COL_W, height = 22,
        text       = "Master volume",
        font       = "button_font",
        textScale = SCALE_BUTTON,
        color      = BTN_FG_COLOR,
        align      = ALIGN_LEFT + ALIGN_MIDDLE,
    }
    rowY = rowY + 22 + 4

    musicSlider = widget.slider{
        x = COL_X, y = rowY, width = COL_W, height = 18,
        min = 0.0, max = 1.0, value = musicVol, step = 0.05,
        onChange = function(self, v) audio.setVolume(v) end,
    }
    rowY = rowY + 18 + BTN_GAP * 2

    local nameLabel = widget.label{
        x = COL_X, y = rowY, width = COL_W, height = 22,
        text       = "Player name",
        font       = "button_font",
        textScale = SCALE_BUTTON,
        color      = BTN_FG_COLOR,
        align      = ALIGN_LEFT + ALIGN_MIDDLE,
    }
    rowY = rowY + 22 + 4

    local nameEdit = widget.lineEdit{
        x = COL_X, y = rowY, width = COL_W, height = 28,
        text       = playerName,
        font       = "button_font",
        textScale = SCALE_BUTTON,
        color      = BTN_FG_COLOR,
        bgColor = {
            enabled  = { 0.10, 0.10, 0.14, 0.85 },
            disabled = { 0.05, 0.05, 0.08, 0.6 },
        },
        padding     = { l = 8, r = 8, t = 4, b = 4 },
        maxLength  = 24,
        placeholder = "Type your name",
        onChange   = function(self, t) optSet("player_name", t) end,
    }
    rowY = rowY + 28 + BTN_GAP * 2

    local function commitAndPop()
        optSave()
        -- Mirror the in-slide: same edge, same duration.
        scene.pop(transition.slide("top", OPT_SLIDE_DUR))
    end

    local back = makeMenuButton(-BTN_W * 0.5, rowY, BTN_W, BTN_H,
                                  "BACK", commitAndPop)

    opt.root:add(musicCheck)
    opt.root:add(volLabel)
    opt.root:add(musicSlider)
    opt.root:add(nameLabel)
    opt.root:add(nameEdit)
    opt.root:add(back)
    -- :add auto-focused musicCheck (first focusable). That's the
    -- desired default, so no override.

    function opt:render()
        -- The OPTIONS title is drawn directly (not in root), so apply
        -- the root's animated offset by hand so it slides with the
        -- widgets below. Dim alpha follows the slide progress so it
        -- doesn't pop in at full opacity while the title is still
        -- off-screen.
        local rx = self.root.x or 0
        local ry = self.root.y or 0
        local d = math.max(math.abs(rx) / self.vw, math.abs(ry) / self.vh)
        if d > 1 then d = 1 end
        local slideProgress = 1 - d
        drawQuad(-self.vw * 0.5, -self.vh * 0.5, self.vw, self.vh,
                  { color = { 0, 0, 0, 0.5 * slideProgress } })
        drawText("OPTIONS", rx, self.titleY + ry, {
            scale = SCALE_MENU_TITLE,
            font  = "menu_title_font",
            align = ALIGN_TOP + ALIGN_CENTER,
            color = { 1, 1, 1 },
        })
        self.root:draw()
    end

    function opt:keyDown(name)
        -- Esc commits + leaves. Backspace also commits — UNLESS the
        -- focused widget consumes it (LineEdit eats Backspace to delete
        -- a character). Probe the focused leaf directly so we can tell
        -- "consumed" from "ignored".
        if name == "escape" then
            commitAndPop()
            return
        end
        if name == "backspace" then
            local fc = self.root.focusedChild
            if fc and fc.keyDown and fc:keyDown(name) then return end
            commitAndPop()
            return
        end
        self.root:keyDown(name)
    end

    return opt
end

-- ---- Confirm dialog ----------------------------------------------------
--
-- onOk: a function invoked when OK is chosen (called AFTER the dialog
-- pops, so appQuit() / appNewGame() / etc. queue their pendingAction
-- and main.cpp picks it up the same frame).

function M.confirmDialog(title, message, onOk)
    local dlg = {
        transparent = true,
        root        = widget.panel({ x = 0, y = 0 }),
        title       = title,
        message     = message,
    }

    -- Dialog rect + button rects.
    local rx, ry = -DLG_W * 0.5, -DLG_H * 0.5
    local total  = DLG_BTN_W * 2 + 10
    local bx0    = rx + (DLG_W - total) * 0.5
    local by     = ry + DLG_H - DLG_PAD - DLG_BTN_H
    dlg.rect = { x = rx, y = ry, w = DLG_W, h = DLG_H }
    local vw, vh = viewSize()
    dlg.vw, dlg.vh = vw, vh

    -- pop + (optional) fire pattern shared by OK / Cancel / Esc.
    -- With a slide-out, onOk needs to wait until after the dialog has
    -- actually left the screen — firing it during the slide would let
    -- the new game / quit / etc. happen behind the still-visible
    -- dialog. Hand the action off to dlg.onExitFire and let :exit
    -- (called by scene.pop's onActionDone) fire it.
    local function popAndFire(chooseOk)
        if chooseOk and onOk then dlg.onExitFire = onOk end
        scene.pop(transition.slide("bottom", DLG_SLIDE_DUR))
    end

    function dlg:exit()
        if self.onExitFire then
            local fn = self.onExitFire
            self.onExitFire = nil
            fn()
        end
    end

    local ok = makeMenuButton(bx0, by, DLG_BTN_W, DLG_BTN_H, "OK",
                                function() popAndFire(true) end)
    local cancel = makeMenuButton(bx0 + DLG_BTN_W + 10, by, DLG_BTN_W, DLG_BTN_H,
                                    "CANCEL", function() popAndFire(false) end)
    -- Center buttons' text in their slightly-narrower frames.
    ok.textAlign     = ALIGN_CENTER + ALIGN_MIDDLE
    cancel.textAlign = ALIGN_CENTER + ALIGN_MIDDLE

    dlg.root:add(ok)
    dlg.root:add(cancel)

    -- :add auto-focused OK (first focusable). Override to Cancel — the
    -- safer default for a destructive prompt.
    ok.focused                = false
    dlg.root.focusedChild    = cancel
    cancel.focused            = true

    function dlg:render()
        -- Slide offset from root. Buttons inside root slide naturally;
        -- the body bg / outline / title / message are direct draws so
        -- we apply (ox, oy) by hand. Dim alpha is tied to slide
        -- progress so the backdrop fades in / out alongside the body
        -- instead of popping in at 0.5 from frame 1.
        local ox = self.root.x or 0
        local oy = self.root.y or 0
        local d  = math.max(math.abs(ox) / self.vw, math.abs(oy) / self.vh)
        if d > 1 then d = 1 end
        local slideProgress = 1 - d
        drawQuad(-self.vw * 0.5, -self.vh * 0.5, self.vw, self.vh,
                  { color = { 0, 0, 0, 0.5 * slideProgress } })
        local r = self.rect
        -- Panel body. dialog_bg is a 32×32 tile asset in the original C
        -- menu; for now fill flat — wire the tile later by registering
        -- it as a slice-carrying region.
        drawQuad(r.x + ox, r.y + oy, r.w, r.h, { color = { 0.14, 0.14, 0.22, 1.0 } })
        widget.drawOutline(r.x + ox, r.y + oy, r.w, r.h,
                            2, { 0.9, 0.9, 1.0, 0.5 })
        drawText(self.title, r.x + r.w * 0.5 + ox, r.y + DLG_PAD + oy, {
            scale = SCALE_DIALOG_TITLE, font = "dialog_title",
            align = ALIGN_TOP + ALIGN_CENTER, color = { 1, 1, 1 },
        })
        drawText(self.message, r.x + r.w * 0.5 + ox, r.y + r.h * 0.5 + oy, {
            scale = SCALE_DIALOG_MSG, font = "button_font",
            align = ALIGN_MIDDLE + ALIGN_CENTER, color = { 1, 1, 1 },
        })
        self.root:draw()
    end

    function dlg:keyDown(name)
        if name == "escape" then
            popAndFire(false)
            return
        end
        self.root:keyDown(name)
    end

    return dlg
end

return M
