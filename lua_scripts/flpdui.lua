-- FLPDUI - the player-facing half of the PDv2 UI: the generation panel and the
-- in-run HUD.
--
-- Delivered to the client by AIO like flprocdungeon.lua, and bound by the same
-- rule: NOTHING here decides anything. Every bound, every grid step and the
-- loot multiplier arrive from C++ in the C payload; this file formats numbers
-- and sends raw user intents back. A Lua copy of a server-owned value is what
-- made the dungeon-challenge affix display disagree with the server it
-- described, and that is the one bug this addon is built to be unable to have.
-- Where a value has not arrived yet the widget reads "..." and the panel asks
-- again - it never guesses, and there is no `or <number>` in this file.
--
--   server whisper  FLPDU\t<kind> <fields...>
--                   C panel state | M block map | R run tick | E completion |
--                   N one-line notice
--   client whisper  FLPD\tUI <verb> [args]
--                   HELLO | SET <key> <int> | GEN | ENTER | HUD <0|1>
--
-- The one number the server does not own is where the frames sit on screen.

local AIO = rawget(_G, "AIO")
if AIO and AIO.AddAddon and AIO.AddAddon() then
    return -- server context: queued for client delivery
end
if _G.FLPDUILoaded then return end
_G.FLPDUILoaded = true

local PREFIX_DOWN = "FLPDU"   -- server -> client
local PREFIX_UP = "FLPD"      -- client -> server, shared with flprocdungeon

-- Client-side cosmetics only. None of these describes the dungeon.
local SET_DEBOUNCE = 0.3        -- seconds a slider may keep moving before it speaks
local HELLO_RETRY = 5           -- one guarded re-ask; see the loading-screen note
local TICK = 0.1                -- how often the driver looks at its own timers
local TOAST_SECONDS = 5
local HUD_HOLD_SECONDS = 60     -- how long a finished run's HUD stays up
local FLASH_SECONDS = 3
local FLASH_PERIOD = 0.25

-- The HUD toggle is the player's, so it survives a relog here rather than on
-- the server (which treats it as session state and forgets it at logout).
FLPDUI_Prefs = FLPDUI_Prefs or {}
if AIO and AIO.AddSavedVarChar then
    AIO.AddSavedVarChar("FLPDUI_Prefs")
end

-- ============================================================================
-- State
-- ============================================================================

local cfg = nil             -- last C payload, nil until the server has spoken
local mapData = nil         -- last M payload
local run = nil             -- last R payload
local setLoop = false       -- true while widgets are written FROM a payload
local pending = {}          -- setKey -> value waiting for the debounce
local pendingAt = 0         -- GetTime() the pending sets go out, 0 = nothing
local wantPanel = false     -- open the panel as soon as a C arrives
local helloSent = false     -- once per Lua state, like flprocdungeon's VER
local helloRetryAt = 0
local completed = false     -- the current run reported state 2
local flashUntil = 0
local flashPhase = false
local hideAt = 0

local hudEnabled = true
if FLPDUI_Prefs.hud == false then
    hudEnabled = false
end

-- ============================================================================
-- Wire helpers
-- ============================================================================

local function SendUp(text)
    SendAddonMessage(PREFIX_UP, text, "WHISPER", UnitName("player"))
end

-- mm:ss, floored and clamped - the same shape the dungeon-challenge tracker
-- uses. The SECONDS are the server's; this only formats them, so the HUD can
-- never drift away from the run it is timing.
local function FmtTime(sec)
    sec = math.max(0, math.floor(sec))
    return string.format("%02d:%02d", math.floor(sec / 60), sec % 60)
end

-- Splits the first `count` space-separated fields off `body` and keeps the rest
-- as `tail`. nil when the payload is short: a half-read payload is never
-- applied, because half a panel is a panel that lies.
local function SplitHead(body, count)
    local out = {}
    local pos = 1
    local len = string.len(body)
    for i = 1, count do
        if pos > len then return nil end
        local at = string.find(body, " ", pos, true)
        if at then
            out[i] = string.sub(body, pos, at - 1)
            pos = at + 1
        else
            out[i] = string.sub(body, pos)
            pos = len + 1
        end
        if out[i] == "" then return nil end
    end
    out.tail = string.sub(body, pos)
    return out
end

local function ToNumbers(fields, count)
    for i = 1, count do
        local value = tonumber(fields[i])
        if not value then return false end
        fields[i] = value
    end
    return true
end

local CFG_FIELDS = 23

local function ParseCfg(body)
    local f = SplitHead(body, CFG_FIELDS)
    if not f or not ToNumbers(f, CFG_FIELDS) then return nil end

    local c = {
        dlvl = f[1], dxp = f[2], xpPerDlvl = f[3], xpPerRoom = f[4],
        rooms = f[5], roomsMin = f[6], roomsMax = f[7],
        diff = f[8], diffMin = f[9], diffMax = f[10], diffStep = f[11],
        caster = f[12], casterMin = f[13], casterMax = f[14],
        bandMin = f[15], bandLo = f[16], bandHi = f[17], bandStep = f[18],
        bandLocked = f[19], lootMultX100 = f[20],
        curRooms = f[21], curBoss = f[22], verdictCode = f[23],
        verdictText = f.tail,
    }

    -- A payload that cannot describe a slider is dropped WHOLE rather than
    -- patched: substituting a step or a bound here is exactly the drift this
    -- addon refuses to have, and the panel simply keeps asking.
    if c.diffStep <= 0 or c.bandStep <= 0 then return nil end
    if c.roomsMin > c.roomsMax or c.diffMin > c.diffMax or
       c.casterMin > c.casterMax or c.bandLo > c.bandHi then
        return nil
    end
    return c
end

local function ParseMap(body)
    local f = SplitHead(body, 5)
    if not f or not ToNumbers(f, 5) then return nil end

    local m = { w = f[1], h = f[2], cpb = f[3], ex = f[4], ey = f[5], blocks = {} }
    if m.w <= 0 or m.h <= 0 or m.cpb <= 0 then return nil end

    -- The fourth field is the block's socket mask (N=1 E=2 S=4 W=8): the
    -- REAL connections, which is what the map draws corridors along. Cell
    -- adjacency alone suggested doors that were not there.
    for bx, by, role, mask in string.gmatch(f.tail, "(%-?%d+),(%-?%d+),(%a),(%d+);") do
        table.insert(m.blocks, { bx = tonumber(bx), by = tonumber(by), role = role,
                                 mask = tonumber(mask) })
    end
    if #m.blocks == 0 then return nil end
    return m
end

local function ParseRun(body)
    local f = SplitHead(body, 10)
    if not f or not ToNumbers(f, 10) then return nil end
    return {
        elapsed = f[1], killed = f[2], total = f[3],
        bossKilled = f[4], bossTotal = f[5],
        roomsCleared = f[6], roomsTotal = f[7],
        px = f[8], py = f[9], state = f[10],
    }
end

local function ParseEnd(body)
    local f = SplitHead(body, 3)
    if not f or not ToNumbers(f, 3) then return nil end
    return { dxp = f[1], dlvl = f[2], leveledUp = f[3] }
end

-- ============================================================================
-- Generation panel
-- ============================================================================

local PANEL_W = 420
local PANEL_H = 350            -- +20 for the current-depths line (2026-08-07)
local BAND_ROW_H = 52           -- what the hidden band row would add back
local BAR_W = PANEL_W - 48

local Panel = CreateFrame("Frame", "FLPDGenPanel", UIParent)
Panel:SetWidth(PANEL_W)
Panel:SetHeight(PANEL_H)
Panel:SetPoint("CENTER", 0, 40)
Panel:SetMovable(true)
Panel:EnableMouse(true)
Panel:RegisterForDrag("LeftButton")
Panel:SetScript("OnDragStart", Panel.StartMoving)
Panel:SetScript("OnDragStop", Panel.StopMovingOrSizing)
Panel:SetBackdrop({
    bgFile = "Interface/Tooltips/UI-Tooltip-Background",
    edgeFile = "Interface/Tooltips/UI-Tooltip-Border",
    tile = true, tileSize = 16, edgeSize = 16,
    insets = { left = 4, right = 4, top = 4, bottom = 4 }
})
Panel:SetBackdropColor(0.05, 0.05, 0.1, 0.95)
Panel:SetBackdropBorderColor(0.4, 0.4, 0.8, 0.8)
Panel:SetFrameStrata("DIALOG")
Panel:Hide()
table.insert(UISpecialFrames, "FLPDGenPanel")
if AIO and AIO.SavePosition then
    AIO.SavePosition(Panel, true)
end

local panelClose = CreateFrame("Button", nil, Panel, "UIPanelCloseButton")
panelClose:SetPoint("TOPRIGHT", -2, -2)
panelClose:SetScript("OnClick", function() Panel:Hide() end)

local title = Panel:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
title:SetPoint("TOP", 0, -12)
title:SetText("|cffFFD700The Forgotten Depths|r")

local levelLine = Panel:CreateFontString(nil, "OVERLAY", "GameFontNormal")
levelLine:SetPoint("TOP", title, "BOTTOM", 0, -6)
levelLine:SetText("...")

local xpBg = Panel:CreateTexture(nil, "ARTWORK")
xpBg:SetPoint("TOP", levelLine, "BOTTOM", 0, -5)
xpBg:SetWidth(BAR_W)
xpBg:SetHeight(8)
xpBg:SetTexture(0.15, 0.15, 0.25, 0.9)

local xpFill = Panel:CreateTexture(nil, "OVERLAY")
xpFill:SetPoint("TOPLEFT", xpBg, "TOPLEFT", 0, 0)
xpFill:SetWidth(1)
xpFill:SetHeight(8)
xpFill:SetTexture(0.35, 0.65, 1.0, 0.95)

local sep1 = Panel:CreateTexture(nil, "ARTWORK")
sep1:SetPoint("TOP", xpBg, "BOTTOM", 0, -8)
sep1:SetWidth(BAR_W)
sep1:SetHeight(1)
sep1:SetTexture(0.4, 0.4, 0.6, 0.5)

-- How each slider renders one value. The x100 divisions decode the wire's
-- fixed point (the field is NAMED x100); they do not recompute anything -
-- difficulty and the loot multiplier are both decided in PDv2GameMath.h.
local function RenderRooms(v) return string.format("%d", v) end
local function RenderDiff(v) return string.format("%.2fx", v / 100) end
local function RenderCaster(v) return string.format("%d%%", v) end
local function RenderBand(v)
    if not cfg then return "..." end
    return string.format("%d-%d", v, v + cfg.bandStep - 1)
end

local function MakeSlider(name, label, render, setKey, anchor, dy)
    local s = CreateFrame("Slider", name, Panel, "OptionsSliderTemplate")
    s:SetWidth(BAR_W - 24)
    s:SetPoint("TOP", anchor, "BOTTOM", 0, dy)
    s:SetMinMaxValues(0, 1)
    s:SetValueStep(1)
    s:SetValue(0)
    s.label = label
    s.render = render
    s.setKey = setKey
    -- OptionsSliderTemplate builds these three FontStrings as globals from the
    -- slider's own name; in 3.3.5 there is no other handle on them.
    s.lowText = _G[name .. "Low"]
    s.highText = _G[name .. "High"]
    s.valueText = _G[name .. "Text"]
    s.lowText:SetText("")
    s.highText:SetText("")
    s.valueText:SetText(label .. " ...")
    s:Disable()
    s:SetScript("OnValueChanged", function(self, value)
        -- setLoop is up whenever the widgets are being written from a server
        -- payload; without it every echo would bounce straight back as a SET.
        if setLoop then return end
        value = math.floor(value + 0.5)
        self.valueText:SetText(self.label .. " " .. self.render(value))
        pending[self.setKey] = value
        pendingAt = GetTime() + SET_DEBOUNCE
    end)
    return s
end

-- Step 1 on rooms and casters is the unit of the quantity itself - there is no
-- fractional room and no fractional percent. The DIFFICULTY grid is a design
-- decision (01 §8), so its step travels on the wire like every other bound.
local roomsSlider = MakeSlider("FLPDRoomsSlider", "Rooms", RenderRooms, "rooms", sep1, -20)
local diffSlider = MakeSlider("FLPDDiffSlider", "Difficulty", RenderDiff, "diff", roomsSlider, -26)
local casterSlider = MakeSlider("FLPDCasterSlider", "Casters", RenderCaster, "caster", diffSlider, -26)
local bandSlider = MakeSlider("FLPDBandSlider", "Mob level", RenderBand, "band", casterSlider, -26)
bandSlider:Hide()

local lootLine = Panel:CreateFontString(nil, "OVERLAY", "GameFontNormal")
lootLine:SetPoint("TOP", casterSlider, "BOTTOM", 0, -16)
lootLine:SetText("...")

-- What the account's CURRENT layout holds - which can differ from the sliders:
-- a stored dungeon keeps its frozen generation inputs, the sliders only shape
-- the NEXT roll. The first in-game test read that difference as a bug, so the
-- panel says it out loud.
local curLine = Panel:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
curLine:SetPoint("TOP", lootLine, "BOTTOM", 0, -6)
curLine:SetText("")

local sep2 = Panel:CreateTexture(nil, "ARTWORK")
sep2:SetPoint("TOP", curLine, "BOTTOM", 0, -8)
sep2:SetWidth(BAR_W)
sep2:SetHeight(1)
sep2:SetTexture(0.4, 0.4, 0.6, 0.5)

local genBtn = CreateFrame("Button", nil, Panel, "UIPanelButtonTemplate")
genBtn:SetWidth(150)
genBtn:SetHeight(24)
genBtn:SetPoint("TOPLEFT", sep2, "BOTTOM", -156, -10)
genBtn:SetText("Generate")
genBtn:Disable()
genBtn:SetScript("OnClick", function() SendUp("UI GEN") end)

local enterBtn = CreateFrame("Button", nil, Panel, "UIPanelButtonTemplate")
enterBtn:SetWidth(150)
enterBtn:SetHeight(24)
enterBtn:SetPoint("TOPRIGHT", sep2, "BOTTOM", 156, -10)
enterBtn:SetText("Enter")
enterBtn:Disable()
enterBtn:SetScript("OnClick", function() SendUp("UI ENTER") end)

-- Pinned to the bottom rather than chained under the buttons, so it stays put
-- when the band row grows the panel.
local verdictLine = Panel:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
verdictLine:SetPoint("BOTTOM", Panel, "BOTTOM", 0, 12)
verdictLine:SetWidth(BAR_W)
verdictLine:SetJustifyH("CENTER")
verdictLine:SetText("Client link: ...")

-- The band row is built exactly like the others and hidden on the SERVER's
-- flag, so the day a multi-band pack set exists the server clears bandLocked
-- and the row appears - no new client code, and its limits are already on the
-- wire waiting for it.
local function LayoutBandRow(show)
    lootLine:ClearAllPoints()
    if show then
        bandSlider:Show()
        lootLine:SetPoint("TOP", bandSlider, "BOTTOM", 0, -16)
        Panel:SetHeight(PANEL_H + BAND_ROW_H)
    else
        bandSlider:Hide()
        lootLine:SetPoint("TOP", casterSlider, "BOTTOM", 0, -16)
        Panel:SetHeight(PANEL_H)
    end
end

local function ApplySlider(s, value, lo, hi, step)
    s:SetMinMaxValues(lo, hi)
    s:SetValueStep(step)
    s.lowText:SetText(s.render(lo))
    s.highText:SetText(s.render(hi))
    s:SetValue(value)
    s.valueText:SetText(s.label .. " " .. s.render(value))
    s:Enable()
end

local function ApplyCfg(c)
    cfg = c
    setLoop = true

    -- Derived DISPLAY math over authoritative inputs: dlvl, dxp and xpPerDlvl
    -- all come from the server, and (dlvl + 1) * xpPerDlvl is the threshold
    -- they already describe. No gameplay constant is invented.
    local nextAt = (c.dlvl + 1) * c.xpPerDlvl
    levelLine:SetText(string.format(
        "Dungeon level |cffFFD700%d|r   %d / %d XP", c.dlvl, c.dxp, nextAt))
    local frac = 0
    if nextAt > 0 then frac = c.dxp / nextAt end
    if frac < 0 then frac = 0 elseif frac > 1 then frac = 1 end
    xpFill:SetWidth(math.max(1, frac * BAR_W))

    ApplySlider(roomsSlider, c.rooms, c.roomsMin, c.roomsMax, 1)
    ApplySlider(diffSlider, c.diff, c.diffMin, c.diffMax, c.diffStep)
    ApplySlider(casterSlider, c.caster, c.casterMin, c.casterMax, 1)
    ApplySlider(bandSlider, c.bandMin, c.bandLo, c.bandHi, c.bandStep)
    LayoutBandRow(c.bandLocked == 0)

    lootLine:SetText(string.format("Loot  |cffFFD700x%.2f|r", c.lootMultX100 / 100))

    if c.curRooms > 0 then
        curLine:SetText(string.format(
            "|cffaaaaaaCurrent depths: %d rooms - %d boss|r", c.curRooms, c.curBoss))
    else
        curLine:SetText("|cffaaaaaaNo depths rolled yet|r")
    end

    -- "code 0 means go" is the wire contract, and the server pins it with a
    -- static_assert so a reordered enum breaks the build instead of the line.
    local text = c.verdictText
    if text == "" then text = "?" end
    local colour = "|cffff8000"
    if c.verdictCode == 0 then colour = "|cff00ff00" end
    verdictLine:SetText("Client link: " .. colour .. text .. "|r")

    genBtn:Enable()
    enterBtn:Enable()

    setLoop = false

    if wantPanel then
        wantPanel = false
        Panel:Show()
    end
end

-- ============================================================================
-- Run HUD
-- ============================================================================

local HUD_W = 240
local CANVAS = 160

local Hud = CreateFrame("Frame", "FLPDHud", UIParent)
Hud:SetWidth(HUD_W)
Hud:SetHeight(240)
Hud:SetPoint("TOP", UIParent, "TOP", 0, -35)
Hud:SetMovable(true)
Hud:EnableMouse(true)
Hud:RegisterForDrag("LeftButton")
Hud:SetScript("OnDragStart", Hud.StartMoving)
Hud:SetScript("OnDragStop", Hud.StopMovingOrSizing)
Hud:SetBackdrop({
    bgFile = "Interface/Tooltips/UI-Tooltip-Background",
    edgeFile = "Interface/Tooltips/UI-Tooltip-Border",
    tile = true, tileSize = 16, edgeSize = 16,
    insets = { left = 4, right = 4, top = 4, bottom = 4 }
})
Hud:SetBackdropColor(0.05, 0.05, 0.1, 0.92)
Hud:SetBackdropBorderColor(0.4, 0.4, 0.8, 0.8)
Hud:SetFrameStrata("HIGH")
Hud:Hide()
if AIO and AIO.SavePosition then
    AIO.SavePosition(Hud, true)
end

local hudTimer = Hud:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
hudTimer:SetPoint("TOP", Hud, "TOP", 0, -9)
hudTimer:SetText("...")

local hudCounts = Hud:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
hudCounts:SetPoint("TOP", hudTimer, "BOTTOM", 0, -4)
hudCounts:SetText("...")

local hudSep = Hud:CreateTexture(nil, "ARTWORK")
hudSep:SetPoint("TOP", hudCounts, "BOTTOM", 0, -5)
hudSep:SetWidth(HUD_W - 20)
hudSep:SetHeight(1)
hudSep:SetTexture(0.4, 0.4, 0.6, 0.5)

local canvas = CreateFrame("Frame", nil, Hud)
canvas:SetWidth(CANVAS)
canvas:SetHeight(CANVAS)
canvas:SetPoint("TOP", hudSep, "BOTTOM", 0, -8)

-- Colour per block role. The letters are the protocol's, the colours are ours.
local ROLE_COLOUR = {
    R = { 0.34, 0.44, 0.64 },   -- room: steel
    E = { 0.20, 0.70, 0.30 },   -- entrance: green
    B = { 0.62, 0.16, 0.16 },   -- boss room: dark red
    c = { 0.30, 0.30, 0.30 },   -- corridor: grey
}

local cells = {}
local cellsUsed = 0

local dot = canvas:CreateTexture(nil, "OVERLAY")
dot:SetWidth(6)
dot:SetHeight(6)
dot:SetTexture(1.0, 0.95, 0.20, 1.0)
dot:Hide()

-- One pooled rectangle. A block is no longer one texture: a corridor is up to
-- four bars, so the pool hands out however many a layout needs and hides the
-- rest.
local function Rect(x, y, w, h, colour)
    cellsUsed = cellsUsed + 1
    local t = cells[cellsUsed]
    if not t then
        t = canvas:CreateTexture(nil, "ARTWORK")
        cells[cellsUsed] = t
    end
    t:SetTexture(colour[1], colour[2], colour[3], 0.9)
    t:ClearAllPoints()
    t:SetPoint("TOPLEFT", canvas, "TOPLEFT", x, -y)
    t:SetWidth(math.max(1, w))
    t:SetHeight(math.max(1, h))
    t:Show()
end

-- Normalise and place, the FLContinentNav idiom: the payload's blocks are
-- already relative to the plan's bounding box, so one division per axis maps
-- them onto whatever the canvas happens to be.
--
-- Rooms are near-full cells. A corridor is drawn as THIN BARS along its
-- socket mask (N=1 E=2 S=4 W=8, matching the planner: N is dy = -1, which is
-- UP on this canvas) - never as a full cell, because a full cell touches all
-- four neighbours and the first in-game test read that as doors that do not
-- exist. Rooms never touch each other (the planner keeps them 2 apart), so
-- every real connection is a corridor bar reaching the room's edge.
local function BuildMap(m)
    mapData = m
    cellsUsed = 0
    local bw = CANVAS / m.w
    local bh = CANVAS / m.h
    local bar = math.max(3, math.floor(math.min(bw, bh) / 3))

    for _, b in ipairs(m.blocks) do
        local colour = ROLE_COLOUR[b.role] or ROLE_COLOUR.c
        local x0 = b.bx * bw
        local y0 = b.by * bh
        if b.role == "c" then
            local cx = x0 + bw / 2
            local cy = y0 + bh / 2
            local mask = b.mask or 0
            if mask == 0 then
                Rect(cx - bar / 2, cy - bar / 2, bar, bar, colour)
            end
            if mask >= 8 then                               -- W: toward x0
                Rect(x0, cy - bar / 2, bw / 2 + bar / 2, bar, colour)
                mask = mask - 8
            end
            if mask >= 4 then                               -- S: toward y0 + bh
                Rect(cx - bar / 2, cy - bar / 2, bar, bh / 2 + bar / 2, colour)
                mask = mask - 4
            end
            if mask >= 2 then                               -- E: toward x0 + bw
                Rect(cx - bar / 2, cy - bar / 2, bw / 2 + bar / 2, bar, colour)
                mask = mask - 2
            end
            if mask >= 1 then                               -- N: toward y0
                Rect(cx - bar / 2, y0, bar, bh / 2 + bar / 2, colour)
            end
        else
            Rect(x0 + 1, y0 + 1, bw - 2, bh - 2, colour)
        end
    end

    for i = cellsUsed + 1, #cells do
        cells[i]:Hide()
    end
end

local function PlaceDot(px, py)
    -- -1 is the server saying "nothing to place": no layout, or the player is
    -- off the plan's bounding box, which on this map means falling.
    if not mapData or px < 0 or py < 0 then
        dot:Hide()
        return
    end
    -- cellsPerBlock rides the M payload precisely so this line can exist
    -- without a copy of the kit's cell count living in Lua.
    local cellsX = mapData.w * mapData.cpb
    local cellsY = mapData.h * mapData.cpb
    dot:ClearAllPoints()
    dot:SetPoint("CENTER", canvas, "TOPLEFT",
        (px + 0.5) * (CANVAS / cellsX), -((py + 0.5) * (CANVAS / cellsY)))
    dot:Show()
end

local function RenderCounts(r, flashOn)
    local bossColour = "|cffffffff"
    if r.state == 2 and not flashOn then
        bossColour = "|cff00ff00"
    end
    hudCounts:SetText(string.format(
        "Mobs |cffffffff%d/%d|r  %s%d/%d bosses|r  |cffffffff%d/%d rooms|r",
        r.killed, r.total, bossColour, r.bossKilled, r.bossTotal,
        r.roomsCleared, r.roomsTotal))
end

local function ApplyRun(r)
    run = r
    if not hudEnabled then
        Hud:Hide()
        return
    end

    if r.state == 0 then
        Hud:Hide()
        return
    end

    -- The clock is the SERVER's: elapsedSec arrives already counted, so a
    -- frozen or a paused run needs no client-side arithmetic to look right.
    hudTimer:SetText(FmtTime(r.elapsed))
    PlaceDot(r.px, r.py)

    if r.state == 2 then
        if not completed then
            completed = true
            flashUntil = GetTime() + FLASH_SECONDS
            flashPhase = false
            hideAt = GetTime() + HUD_HOLD_SECONDS
        end
    else
        completed = false
        flashUntil = 0
        hideAt = 0
    end

    RenderCounts(r, flashPhase)

    -- A finished run's HUD is shown for its hold and then stays gone: once the
    -- auto-hide has fired, hideAt is 0 and a repeated state-2 frame (a HELLO
    -- inside a cleared dungeon sends one) must not drag it back up.
    if r.state == 1 or hideAt > 0 then
        Hud:Show()
    end
end

local hudAccum = 0
Hud:SetScript("OnUpdate", function(self, elapsed)
    hudAccum = hudAccum + elapsed
    if hudAccum < FLASH_PERIOD then return end
    hudAccum = 0
    if not run then return end

    local now = GetTime()
    if flashUntil > 0 then
        if now >= flashUntil then
            flashUntil = 0
            flashPhase = false
        else
            flashPhase = not flashPhase
        end
        RenderCounts(run, flashPhase)
    end
    if hideAt > 0 and now >= hideAt then
        hideAt = 0
        self:Hide()
    end
end)

-- ============================================================================
-- Completion toast
-- ============================================================================

local Toast = CreateFrame("Frame", "FLPDToast", UIParent)
Toast:SetWidth(460)
Toast:SetHeight(26)
Toast:SetPoint("TOP", UIParent, "TOP", 0, -150)
Toast:SetFrameStrata("HIGH")
Toast:Hide()

local toastText = Toast:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
toastText:SetAllPoints(Toast)
toastText:SetJustifyH("CENTER")

local toastUntil = 0
Toast:SetScript("OnUpdate", function(self)
    local left = toastUntil - GetTime()
    if left <= 0 then
        self:Hide()
        self:SetAlpha(1)
        return
    end
    if left < 1 then
        self:SetAlpha(left)
    end
end)

local function ApplyEnd(e)
    if e.leveledUp == 1 then
        toastText:SetText(string.format(
            "|cffFFD700+%d Dungeon XP  -  Level %d!|r", e.dxp, e.dlvl))
    else
        toastText:SetText(string.format("|cffFFD700+%d Dungeon XP|r", e.dxp))
    end
    Toast:SetAlpha(1)
    toastUntil = GetTime() + TOAST_SECONDS
    Toast:Show()
end

-- ============================================================================
-- Driver: events, and the two client-side timers
-- ============================================================================

local driver = CreateFrame("Frame")
driver:RegisterEvent("PLAYER_ENTERING_WORLD")
driver:RegisterEvent("CHAT_MSG_ADDON")

driver:SetScript("OnEvent", function(self, event, arg1, arg2)
    if event == "PLAYER_ENTERING_WORLD" then
        -- Any map change makes the run frame stale until the next R says
        -- otherwise - including the one that carries the player back OUT.
        Hud:Hide()

        -- Once per Lua state, like flprocdungeon's version report: this event
        -- fires again for the dungeon's own loading screen, and a HELLO per
        -- loading screen would ask the server to re-answer for nothing.
        if not helloSent then
            helloSent = true
            SendUp("UI HELLO")
            if not hudEnabled then
                SendUp("UI HUD 0")   -- the server starts every session HUD-on
            end
            -- ONE guarded re-ask, for the reason flprocdungeon has one:
            -- server-to-client addon chat DIES during a loading screen while
            -- client-to-server survives it (measured 2026-08-06), so the
            -- answer to the very first HELLO can simply vanish.
            helloRetryAt = GetTime() + HELLO_RETRY
        end
        return
    end

    if event ~= "CHAT_MSG_ADDON" then return end
    if arg1 ~= PREFIX_DOWN then
        return                     -- also skips the echo of our own FLPD whispers
    end
    if not arg2 or string.len(arg2) < 2 or string.sub(arg2, 2, 2) ~= " " then return end

    local kind = string.sub(arg2, 1, 1)
    local body = string.sub(arg2, 3)

    -- Malformed payloads are dropped in silence. A client that argues with its
    -- server in the chat frame is a client nobody keeps installed.
    if kind == "C" then
        local c = ParseCfg(body)
        if c then ApplyCfg(c) end
    elseif kind == "M" then
        local m = ParseMap(body)
        if m then BuildMap(m) end
    elseif kind == "R" then
        local r = ParseRun(body)
        if r then ApplyRun(r) end
    elseif kind == "E" then
        local e = ParseEnd(body)
        if e then ApplyEnd(e) end
    elseif kind == "N" then
        DEFAULT_CHAT_FRAME:AddMessage("|cffFFD700The Forgotten Depths:|r " .. body)
    end
end)

local driverAccum = 0
driver:SetScript("OnUpdate", function(self, elapsed)
    driverAccum = driverAccum + elapsed
    if driverAccum < TICK then return end
    driverAccum = 0

    local now = GetTime()

    -- The debounce: a slider being dragged speaks once, when it settles. The
    -- flush lives here rather than on the panel so closing the panel mid-drag
    -- cannot swallow the change.
    if pendingAt > 0 and now >= pendingAt then
        pendingAt = 0
        for key, value in pairs(pending) do
            SendUp(string.format("UI SET %s %d", key, value))
        end
        pending = {}
    end

    if helloRetryAt > 0 and now >= helloRetryAt then
        helloRetryAt = 0
        if not cfg then SendUp("UI HELLO") end
    end
end)

-- ============================================================================
-- Slash commands
-- ============================================================================

SLASH_FLPDUI1 = "/pd"
SlashCmdList["FLPDUI"] = function(msg)
    msg = string.lower(msg or "")
    msg = string.gsub(msg, "^%s*(.-)%s*$", "%1")

    if msg == "hud" then
        hudEnabled = not hudEnabled
        FLPDUI_Prefs.hud = hudEnabled
        SendUp("UI HUD " .. (hudEnabled and "1" or "0"))
        if hudEnabled then
            DEFAULT_CHAT_FRAME:AddMessage(
                "|cffFFD700The Forgotten Depths:|r run HUD on.")
        else
            Hud:Hide()
            DEFAULT_CHAT_FRAME:AddMessage(
                "|cffFFD700The Forgotten Depths:|r run HUD off.")
        end
        return
    end

    if Panel:IsShown() then
        Panel:Hide()
        return
    end

    -- Always re-ask on open: the panel shows the account's live settings AND
    -- the client link's live verdict, and both can have moved since the last C.
    SendUp("UI HELLO")
    if cfg then
        Panel:Show()
    else
        wantPanel = true       -- it opens itself the moment the answer lands
    end
end

DEFAULT_CHAT_FRAME:AddMessage(
    "|cffFFD700The Forgotten Depths|r loaded. |cffFFD700/pd|r opens the depths, "
    .. "|cffFFD700/pd hud|r toggles the run HUD.")
