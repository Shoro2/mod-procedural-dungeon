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
--                   C panel state | M block map | K cleared blocks |
--                   R run tick | E completion | N one-line notice
--                   M block letters  E entrance | B boss room | R room |
--                                    V event room | c corridor
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
local clearedSet = {}       -- last K payload: ["bx,by"] = true, cleared rooms
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

-- How many numeric fields the C payload carries before its free-text tail. The
-- server only ever APPENDS to that list and this number moves in lockstep: 28
-- since the chosen theme (27) and the highest theme the loaded kit can build
-- (28) went on the wire ahead of the tail, behind the stat profile (25) and its
-- unlock flag (26). A worldserver from before the theme pair sends 26, its
-- verdict text lands in fields 27/28, ToNumbers refuses it and the payload is
-- dropped WHOLE - the panel keeps asking and reads "..." rather than showing a
-- theme nobody sent. That is the intended failure and not a compatibility bug:
-- this addon and the worldserver that speaks to it are deployed together.
local CFG_FIELDS = 28

local function ParseCfg(body)
    local f = SplitHead(body, CFG_FIELDS)
    if not f or not ToNumbers(f, CFG_FIELDS) then return nil end

    local c = {
        -- xpInto / xpNeed are per LEVEL, not lifetime: the server walks the
        -- 10 %-per-level cost chain and sends the pair the bar needs. There is
        -- no arithmetic to do here, which is the point.
        dlvl = f[1], xpInto = f[2], xpNeed = f[3], xpPerRoom = f[4],
        rooms = f[5], roomsMin = f[6], roomsMax = f[7],
        diff = f[8], diffMin = f[9], diffMax = f[10], diffStep = f[11],
        caster = f[12], casterMin = f[13], casterMax = f[14],
        bandMin = f[15], bandLo = f[16], bandHi = f[17], bandStep = f[18],
        bandLocked = f[19], lootMultX100 = f[20],
        curRooms = f[21], curBoss = f[22], affixCount = f[23],
        verdictCode = f[24],
        -- The account's chosen loot profile and whether THIS character owns the
        -- Discerning Eye talent that unlocks it. The flag decides whether the
        -- row is drawn at all; it is a hint about the panel, never a permission
        -- - the server refuses `SET statprofile` on its own side while the
        -- talent is missing, exactly as it does for the band row.
        statProfile = f[25], statUnlocked = f[26],
        -- The account's theme for the NEXT generation (0 = follow the server's
        -- own V2.Theme) and the highest theme id the loaded kit can build. Both
        -- are the server's word: this panel knows neither which themes exist
        -- nor what the conf default is, it only moves the knob between 0 and
        -- the ceiling it was handed - which is how a kit that learns a new
        -- theme widens this row without a new addon.
        cfgTheme = f[27], themeMax = f[28],
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
    -- The theme slider's LOW bound does not travel, because it is the wire
    -- contract itself: 0 ("follow the server's V2.Theme") is always a legal
    -- choice, so a themeMax under it is the same "lo > hi" the line above
    -- refuses. A server with exactly one theme sends 0 and gets a one-stop
    -- slider reading "Standard", which is the honest picture of it.
    if c.themeMax < 0 then return nil end
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

    -- The gate fields (segPlanned segKilled segPct for the next SEALED
    -- barrier) are read from the tail and are OPTIONAL - the one tolerance in
    -- this file, and a narrow one: a worldserver from before C7 sends ten
    -- fields, and the honest answer to "which gate?" is then "the server did
    -- not say", which is what three zeros mean. It is NOT a guess about the
    -- dungeon; the HUD prints no gate line's numbers unless the server sent
    -- them. A short or unparsable tail is treated exactly like an absent one.
    local segPlanned, segKilled, segPct = 0, 0, 0
    local g = SplitHead(f.tail, 3)
    if g and ToNumbers(g, 3) then
        segPlanned, segKilled, segPct = g[1], g[2], g[3]
    end

    -- The event fields (eventSec eventPct for the event room being defended)
    -- are the SECOND optional group and sit behind the gate one, because the
    -- server only ever appends: a worldserver from before Round E sends
    -- thirteen fields, one from before C7 sends ten, and both mean "no event"
    -- here. Read off `g.tail` and only when the gate group was there at all,
    -- since this pair can never arrive without it. Two zeros are not a guess
    -- about the dungeon: the server sends exactly them while nothing is
    -- running, and the HUD prints no event line for them either way.
    local eventSec, eventPct = 0, 0
    local h = g and SplitHead(g.tail, 2)
    if h and ToNumbers(h, 2) then
        eventSec, eventPct = h[1], h[2]
    end

    -- The gate's open state (segOpen) is the THIRD optional group and carries
    -- a single field, appended behind the event pair by the rule the server
    -- writes by: a worldserver from before WP8 sends fifteen fields, and 0 is
    -- what its silence means as exactly as what it would have sent - "the
    -- barrier of the segment this line describes is still sealed". That is the
    -- only state the gate line could show before this field existed, so an old
    -- server keeps precisely the line it always drew. Read off `h.tail` and
    -- only when the event pair was there, since this field can never arrive
    -- without it.
    local segOpen = 0
    local k = h and SplitHead(h.tail, 1)
    if k and ToNumbers(k, 1) then
        segOpen = k[1]
    end

    return {
        elapsed = f[1], killed = f[2], total = f[3],
        bossKilled = f[4], bossTotal = f[5],
        roomsCleared = f[6], roomsTotal = f[7],
        px = f[8], py = f[9], state = f[10],
        segPlanned = segPlanned, segKilled = segKilled, segPct = segPct,
        eventSec = eventSec, eventPct = eventPct,
        segOpen = segOpen,
    }
end

-- "bx,by;bx,by;..." in the SAME frame the M payload uses (the server shifts
-- both by the plan's minBX/minBY), keyed the way BuildMap concatenates a
-- block's own numbers so the two sides cannot disagree about "257,258".
-- An empty body is a legal payload and means "nothing cleared yet".
local function ParseCleared(body)
    local set = {}
    for bx, by in string.gmatch(body, "(%-?%d+),(%-?%d+);") do
        local x, y = tonumber(bx), tonumber(by)
        if x and y then
            set[x .. "," .. y] = true
        end
    end
    return set
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
-- +20 current-depths (2026-08-07), +14 affixes, +64 the theme row (2026-09-11).
-- The theme row is NOT optional - every account has a theme and the server
-- sends the pair in every C - so its height belongs to the base panel instead
-- of to one of LayoutPanel's conditional terms. The 64 is summed, not
-- eyeballed, the way the profile row's is: 26 of gap from the difficulty
-- slider down to it + 17 of slider (OptionsSliderTemplate's own height) + 11
-- of gap to its hint line + 10 of hint line. Everything below keeps the 26 it
-- always had to the casters slider, so every row under the new one moves down
-- by exactly these 64 and no combination of the three optional rows needs its
-- own number.
local PANEL_H = 428
local BAND_ROW_H = 52           -- what the hidden band row would add back
-- What the hidden stat-profile row would add back, summed rather than guessed:
-- 26 of gap to the slider + 17 of slider (OptionsSliderTemplate's own height)
-- + 11 of gap to the hint + 10 of hint line. Everything under the row keeps its
-- usual gaps (26 to a slider, 16 to a text line) whether it hangs off the hint
-- or off the casters slider, which is what makes this ONE number.
local PROFILE_ROW_H = 64
local PREVIEW = 160             -- the layout preview: the HUD map's square, verbatim
local PREVIEW_ROW_H = 170       -- what the hidden preview row would add back: 160 + its gap
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

-- How each slider renders one value. The one x100 division left decodes the
-- wire's fixed point (the field is NAMED x100); it does not recompute anything
-- - the loot multiplier is decided in PDv2GameMath.h.
--
-- Difficulty is a PLAIN INTEGER since 2026-08-08: the value on the wire IS the
-- difficulty (1..100), so rendering it is a "%d" and nothing else. The old
-- "%.2fx" divided by 100 because difficulty used to be carried x100 - printing
-- that formula against the new scale would show the player "0.37x" for a
-- difficulty of 37.
local function RenderRooms(v) return string.format("%d", v) end
local function RenderDiff(v) return string.format("%d", v) end
local function RenderCaster(v) return string.format("%d%%", v) end
local function RenderBand(v)
    if not cfg then return "..." end
    return string.format("%d-%d", v, v + cfg.bandStep - 1)
end

-- The theme's value is a WORD too, and it is the one label in this panel the
-- server does not send: the list lives on the CLIENT, indexed by the very id
-- the wire carries, and themeMax says how far it may be read (spec D8). That
-- is not the forbidden copy of a server-owned value - the names are art, not
-- rules, and no decision hangs on them - but the ids underneath them are the
-- contract, so a theme this table has no word for still prints its NUMBER:
-- hiding a choice the server would accept is worse than showing it unnamed, and
-- "..." would claim the server had said nothing when it had.
local THEME_NAMES = { [0] = "Standard", [1] = "Mine", [2] = "Stadt", [3] = "Wald" }
local function RenderTheme(v)
    local name = THEME_NAMES[v]
    if name then return name end
    if type(v) ~= "number" then return "..." end
    return string.format("%d", v)
end

-- The stat profile's value is a WORD as well. 0..3 travels on the wire and
-- lives in the account column because that is what the SET verb and the
-- engine's PD_STAT_PROFILE_* take; the player never sees the number. A value the
-- server clamps to that range can only arrive outside it if the two sides have
-- stopped agreeing, and then this reads "..." like every other widget that has
-- not been told - naming a profile there would be the guess this addon does not
-- make, and indexing the table blind would blank the panel mid-payload.
local PROFILE_NAMES = { [0] = "Off", "Strength", "Agility", "Caster" }
local function RenderProfile(v)
    return PROFILE_NAMES[v] or "..."
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
-- The look is picked before the numbers, so the theme row sits directly under
-- the difficulty slider and above the casters one. Unlike the two rows below
-- it, it is ALWAYS up: there is no flag to hide it behind, every account has a
-- theme and the server sends the pair in every C - which is why it costs no
-- branch in LayoutPanel and its height simply belongs to PANEL_H.
local themeSlider = MakeSlider("FLPDThemeSlider", "Theme", RenderTheme, "theme",
                               diffSlider, -26)

-- What the row does, said once under it the way the stat-profile row says it: a
-- theme takes effect on the NEXT Generate and leaves the depths the account
-- already holds exactly as they were rolled (the server freezes the theme into
-- the stored layout). No SetWidth on purpose - an unwrapped FontString is one
-- line tall, which is the line PANEL_H's 64 pays for - and the copy is kept
-- short enough that it can never run past the panel border, which Panel would
-- not clip.
local themeHint = Panel:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
themeHint:SetPoint("TOP", themeSlider, "BOTTOM", 0, -11)
themeHint:SetText("|cffaaaaaaApplies to the next Generate|r")

local casterSlider = MakeSlider("FLPDCasterSlider", "Casters", RenderCaster, "caster", themeHint, -26)
-- The stat-profile row sits between the casters slider and the band row, in the
-- order the two optional rows were added. Both anchors passed here are the
-- "neither optional row is up" case: from here on LayoutPanel owns where the
-- band slider hangs, because what sits above it depends on the profile row.
local profileSlider = MakeSlider("FLPDProfileSlider", "Stat profile", RenderProfile,
                                 "statprofile", casterSlider, -26)
profileSlider:Hide()
local bandSlider = MakeSlider("FLPDBandSlider", "Mob level", RenderBand, "band", casterSlider, -26)
bandSlider:Hide()

-- What the row is FOR, said once under it. A slider labelled "Stat profile"
-- does not say which loot it shapes, and this panel is the only place a player
-- who just bought Discerning Eye finds that out. Hidden and shown with the
-- slider by LayoutPanel, and no SetWidth on purpose: an unwrapped FontString is
-- one line tall, which is the line PROFILE_ROW_H pays for - so the copy is kept
-- short enough (well under BAR_W at this font) that it can never run past the
-- panel border, which Panel would not clip.
local profileHint = Panel:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
profileHint:SetPoint("TOP", profileSlider, "BOTTOM", 0, -11)
profileHint:SetText("|cffaaaaaaDiscerning Eye: loot follows this profile|r")
profileHint:Hide()

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

-- How many affixes the difficulty on the slider buys. The COUNT is the
-- server's - it comes down in the C payload from the same table the dungeon
-- spawns from - because a Lua-side copy of that number is precisely the bug
-- this addon exists not to have. "0" is a legal, common answer: the affix
-- gates start at difficulty 10.
local affixLine = Panel:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
affixLine:SetPoint("TOP", curLine, "BOTTOM", 0, -4)
affixLine:SetText("")

-- The layout the account is holding, drawn by the HUD's own map code from the
-- M payload the server sends after a generation - the SAME payload the in-run
-- HUD reads, so the two pictures cannot disagree about the dungeon they show.
-- Nothing about the plan is worked out here: the blocks, their roles and their
-- socket masks all arrive finished, and a payload that never comes leaves the
-- row folded away rather than showing an empty box that claims a layout.
local previewCanvas = CreateFrame("Frame", nil, Panel)
previewCanvas:SetWidth(PREVIEW)
previewCanvas:SetHeight(PREVIEW)
previewCanvas:SetPoint("TOP", affixLine, "BOTTOM", 0, -10)
previewCanvas:Hide()

-- What BuildMap draws into. The rectangle pool is the target's own because a
-- texture belongs to the frame it was created on: the HUD and the panel cannot
-- lend each other one, and each hides its own leftovers.
local previewTarget = { canvas = previewCanvas, pool = {}, size = PREVIEW }

local sep2 = Panel:CreateTexture(nil, "ARTWORK")
sep2:SetPoint("TOP", affixLine, "BOTTOM", 0, -8)
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

-- The three optional rows, and the panel height that follows from them.
--
-- The band row is built exactly like the others and hidden on the SERVER's
-- flag, so the day a multi-band pack set exists the server clears bandLocked
-- and the row appears - no new client code, and its limits are already on the
-- wire waiting for it.
--
-- The stat-profile row is that same mechanism read the other way round: it is
-- up only while the server says this character owns Discerning Eye. Hiding it
-- is cosmetic and nothing more - the server refuses the SET behind it, so a
-- client that drew the row anyway would only collect a polite refusal.
--
-- The preview row is the same idea from the other side: it is up only while a
-- layout has actually arrived to draw in it. All three flags are the server's
-- word, never a guess, and PANEL_H is the panel with none of the rows.
local bandRow = false      -- bandSlider up: the server's bandLocked is clear
local profileRow = false   -- profileSlider up: the server's statUnlocked is set
local previewRow = false   -- previewCanvas up: an M payload has been drawn

local function LayoutPanel()
    -- What the rows underneath hang from: the profile row's hint line while
    -- that row is up, the casters slider while it is not. They re-anchor rather
    -- than move, so neither optional row has to know about the other.
    local above = casterSlider
    if profileRow then
        profileSlider:Show()
        profileHint:Show()
        above = profileHint
    else
        profileSlider:Hide()
        profileHint:Hide()
    end

    bandSlider:ClearAllPoints()
    lootLine:ClearAllPoints()
    if bandRow then
        bandSlider:SetPoint("TOP", above, "BOTTOM", 0, -26)
        bandSlider:Show()
        lootLine:SetPoint("TOP", bandSlider, "BOTTOM", 0, -16)
    else
        bandSlider:Hide()
        lootLine:SetPoint("TOP", above, "BOTTOM", 0, -16)
    end

    -- The preview sits between the affix line and the separator, so the band
    -- row above pushes it down with everything else and neither row needs to
    -- know about the other.
    sep2:ClearAllPoints()
    if previewRow then
        previewCanvas:Show()
        sep2:SetPoint("TOP", previewCanvas, "BOTTOM", 0, -8)
    else
        previewCanvas:Hide()
        sep2:SetPoint("TOP", affixLine, "BOTTOM", 0, -8)
    end

    local height = PANEL_H
    if profileRow then height = height + PROFILE_ROW_H end
    if bandRow then height = height + BAND_ROW_H end
    if previewRow then height = height + PREVIEW_ROW_H end
    Panel:SetHeight(height)
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

    -- Both numbers arrive finished. The bar RESTARTS at 0 on every level-up
    -- because xpInto is the remainder into the current level - the old line
    -- multiplied a threshold out of (dlvl + 1) * xpPerDlvl and drew a lifetime
    -- total against it, which stopped being true the moment levels stopped
    -- costing the same (2026-08-08). Nothing is derived here any more.
    levelLine:SetText(string.format(
        "Dungeon level |cffFFD700%d|r   %d / %d XP", c.dlvl, c.xpInto, c.xpNeed))
    local frac = 0
    if c.xpNeed > 0 then frac = c.xpInto / c.xpNeed end
    if frac < 0 then frac = 0 elseif frac > 1 then frac = 1 end
    xpFill:SetWidth(math.max(1, frac * BAR_W))

    ApplySlider(roomsSlider, c.rooms, c.roomsMin, c.roomsMax, 1)
    ApplySlider(diffSlider, c.diff, c.diffMin, c.diffMax, c.diffStep)
    -- 0 is the one bound this slider does not read off the wire, because it is
    -- the wire contract itself: "follow the server's own V2.Theme" is always an
    -- allowed choice. The ceiling IS the server's - themeMax is the highest
    -- theme the loaded kit can build - so a kit that learns a forest raises
    -- this row on its own, and a panel that outlives its server never offers a
    -- theme that worldserver would refuse.
    ApplySlider(themeSlider, c.cfgTheme, 0, c.themeMax, 1)
    ApplySlider(casterSlider, c.caster, c.casterMin, c.casterMax, 1)
    -- The only slider whose bounds BOTH stay off the wire, and the exception
    -- proves the rule: 0..3 is the wire contract itself - the four values the
    -- SET verb accepts and the four names the addon can render - not a tuning
    -- number the server may move under us. A fifth profile is a new payload and
    -- a new addon either way, which is what CFG_FIELDS is there to enforce.
    -- Written whether the row is drawn or not, so the panel already shows the
    -- account's stored choice the moment Discerning Eye raises the row.
    ApplySlider(profileSlider, c.statProfile, 0, 3, 1)
    ApplySlider(bandSlider, c.bandMin, c.bandLo, c.bandHi, c.bandStep)

    bandRow = c.bandLocked == 0
    -- Per CHARACTER, unlike the profile it shows: the talent is bought on one
    -- character, the chosen profile belongs to the account. A character without
    -- the node simply does not see the row the others steer.
    profileRow = c.statUnlocked == 1
    -- curRooms 0 is the server saying the account holds no layout at all, so
    -- whatever the preview last drew describes a dungeon that is gone. The row
    -- folds away and the next M brings it back.
    if c.curRooms == 0 then previewRow = false end
    LayoutPanel()

    lootLine:SetText(string.format("Loot  |cffFFD700x%.2f|r", c.lootMultX100 / 100))

    if c.curRooms > 0 then
        curLine:SetText(string.format(
            "|cffaaaaaaCurrent depths: %d rooms - %d boss|r", c.curRooms, c.curBoss))
    else
        curLine:SetText("|cffaaaaaaNo depths rolled yet|r")
    end

    affixLine:SetText(string.format(
        "|cffaaaaaaAffixes at this difficulty: %d|r", c.affixCount))

    -- "code 0 means go" is the wire contract, and the server pins it with a
    -- static_assert so a reordered enum breaks the build instead of the line.
    local text = c.verdictText
    if text == "" then text = "?" end
    local colour = "|cffff8000"
    if c.verdictCode == 0 then colour = "|cff00ff00" end
    verdictLine:SetText("Client link: " .. colour .. text .. "|r")

    genBtn:Enable()

    -- Enter follows the verdict, not the payload's mere existence: an unready
    -- client would only collect the polite refusal, and a grey button says the
    -- same thing without the round trip. verdictCode is the server's word and
    -- the server pushes a fresh C on every link-state change, so this greys
    -- and lights up by itself as READY comes and goes.
    if c.verdictCode == 0 then
        enterBtn:Enable()
    else
        enterBtn:Disable()
    end

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
local HUD_BAR_W = HUD_W - 20    -- the event bar spans the separator's width

local Hud = CreateFrame("Frame", "FLPDHud", UIParent)
Hud:SetWidth(HUD_W)
-- 270 was this frame with a one-line event row: +16 for the gate line
-- (2026-09-08, C7) and +14 for that line (2026-09-10, Round E). The event BAR
-- that replaced the line (WP8) is four pixels taller than it - 3 of gap, 12 of
-- bar, then 1 and 4 for the health strip under it, where the line took 2 and
-- 14 - so the frame grows by those four rather than eat the map's gap.
--
-- Both rows are RESERVED, not reclaimed: the gate string keeps a fixed height
-- when it is empty and the event textures only drop to alpha 0, so the map
-- below never jumps as a gate opens or a defence starts and ends.
Hud:SetHeight(274)
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

-- The kill progress of the gate the player is standing behind, straight off
-- the run tick. Like every other number on this HUD it is the server's: the
-- addon knows neither the barrier threshold nor which segment it is counting.
--
-- The height is pinned because the line is now EMPTY where the server has no
-- gate to describe (a boss room, a player off the plan): a FontString with no
-- text is zero pixels tall, and letting the row collapse would drag the event
-- bar, the separator and the map up with it every time.
local hudGate = Hud:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
hudGate:SetPoint("TOP", hudCounts, "BOTTOM", 0, -2)
hudGate:SetHeight(14)
hudGate:SetText("...")

-- The running event as a bar: how much of the defence is left to survive, the
-- host's remaining health under it, and the clock written across it. The
-- SECONDS and the PERCENT are the server's - this row only draws them, so it
-- can no more disagree with the defence than the timer can with the run.
--
-- Plain textures with SetWidth, the same idiom the panel's XP bar uses: there
-- is no StatusBar frame anywhere in this addon and this row does not introduce
-- one. Background and fills share the ARTWORK layer (a region created later
-- draws over one created earlier, which is what puts each fill on its
-- background); the text sits a whole layer above, so a full bar can never
-- swallow it.
--
-- The row is RESERVED rather than reclaimed: nothing is hidden or re-anchored
-- when no event runs, the textures simply go to alpha 0 and keep their space,
-- so hudSep and the map below hold still. A map that jumps the moment a timed
-- defence starts is worse than a blank row.
local hudEventBg = Hud:CreateTexture(nil, "ARTWORK")
hudEventBg:SetPoint("TOP", hudGate, "BOTTOM", 0, -3)
hudEventBg:SetWidth(HUD_BAR_W)
hudEventBg:SetHeight(12)
hudEventBg:SetTexture(0.15, 0.15, 0.25, 0.9)
hudEventBg:SetAlpha(0)

local hudEventFill = Hud:CreateTexture(nil, "ARTWORK")
hudEventFill:SetPoint("TOPLEFT", hudEventBg, "TOPLEFT", 0, 0)
hudEventFill:SetWidth(1)
hudEventFill:SetHeight(12)
hudEventFill:SetTexture(0.95, 0.62, 0.15, 0.95)
hudEventFill:SetAlpha(0)

-- The host's health, a thin strip under the bar rather than a second full one:
-- the two numbers race each other during a defence and stacking them keeps the
-- comparison in one glance without spending another line of the HUD.
local hudEventHp = Hud:CreateTexture(nil, "ARTWORK")
hudEventHp:SetPoint("TOPLEFT", hudEventBg, "BOTTOMLEFT", 0, -1)
hudEventHp:SetWidth(1)
hudEventHp:SetHeight(4)
hudEventHp:SetTexture(0.20, 0.70, 0.30, 0.95)
hudEventHp:SetAlpha(0)

local hudEvent = Hud:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
hudEvent:SetPoint("CENTER", hudEventBg, "CENTER", 0, 0)
hudEvent:SetText("")

-- How long the running defence started with. It is NOT on the wire - the tick
-- carries the seconds LEFT - so the longest count seen for the defence being
-- drawn is its length as far as this HUD can honestly know. The server's own
-- V2.Event.DurationSec is deliberately not copied here: a Lua-side copy of a
-- server-owned number is the one bug this addon is built to be unable to have
-- (see the header), and a defence joined halfway through is better drawn
-- against the part this client has actually watched than against a length it
-- guessed and the server may not have used.
--
-- The count only ever falls inside one defence (the server pins a deadline
-- when it starts), so a HIGHER one can only be a new defence: re-pin instead
-- of holding a bar clamped at full. Zero seconds ends the row and clears this,
-- which is also what the server sends between two defences.
local eventSpan = 0

local hudSep = Hud:CreateTexture(nil, "ARTWORK")
-- Anchored to the bar's BACKGROUND, never to the health strip: the strip's
-- width follows the host's health, and a point on a shrinking texture would
-- walk the separator - and the map hanging off it - sideways across the HUD.
-- The offset is the strip's own 1 + 4 plus the 5 this separator always had.
hudSep:SetPoint("TOP", hudEventBg, "BOTTOM", 0, -10)
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
    V = { 0.55, 0.25, 0.75 },   -- event room: violet
}

-- The same blocks once the K payload says every mob in them is dead. Only the
-- two roles that hold mobs have a cleared colour; the entrance is green
-- already and a corridor has no room counter behind it.
--
-- An event room keeps its own colour: it holds no run mobs, so it never turns
-- up in a K payload and has nothing to go green for. The entry is spelled out
-- anyway rather than left to the fallback, so the map says what it means.
local CLEARED_COLOUR = {
    R = { 0.20, 0.75, 0.30 },   -- cleared room: green
    B = { 0.10, 0.50, 0.20 },   -- cleared boss room: darker green
    V = { 0.55, 0.25, 0.75 },   -- event room: violet, cleared or not
}

-- This HUD's draw target, shaped like the panel's preview one: the frame to
-- draw on, that frame's own rectangle pool, and the edge length a plan is
-- normalised onto. Two targets exist and they share nothing but the code.
local hudTarget = { canvas = canvas, pool = {}, size = CANVAS }

local dot = canvas:CreateTexture(nil, "OVERLAY")
dot:SetWidth(6)
dot:SetHeight(6)
dot:SetTexture(1.0, 0.95, 0.20, 1.0)
dot:Hide()

-- One pooled rectangle, from the TARGET's pool. A block is no longer one
-- texture: a corridor is up to four bars, so the pool hands out however many a
-- layout needs and hides the rest. `used` is BuildMap's running count on the
-- target it is drawing, which is what lets the same code fill two frames.
local function Rect(target, x, y, w, h, colour)
    target.used = target.used + 1
    local t = target.pool[target.used]
    if not t then
        t = target.canvas:CreateTexture(nil, "ARTWORK")
        target.pool[target.used] = t
    end
    t:SetTexture(colour[1], colour[2], colour[3], 0.9)
    t:ClearAllPoints()
    t:SetPoint("TOPLEFT", target.canvas, "TOPLEFT", x, -y)
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
--
-- The target says WHERE (frame, pool, edge length); the payload says what. The
-- run HUD and the gen panel's preview are the same picture at the same size,
-- and the only reason there are two is that they hang on different frames.
local function BuildMap(m, target)
    target.used = 0
    local bw = target.size / m.w
    local bh = target.size / m.h
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
                Rect(target, cx - bar / 2, cy - bar / 2, bar, bar, colour)
            end
            if mask >= 8 then                               -- W: toward x0
                Rect(target, x0, cy - bar / 2, bw / 2 + bar / 2, bar, colour)
                mask = mask - 8
            end
            if mask >= 4 then                               -- S: toward y0 + bh
                Rect(target, cx - bar / 2, cy - bar / 2, bar, bh / 2 + bar / 2, colour)
                mask = mask - 4
            end
            if mask >= 2 then                               -- E: toward x0 + bw
                Rect(target, cx - bar / 2, cy - bar / 2, bw / 2 + bar / 2, bar, colour)
                mask = mask - 2
            end
            if mask >= 1 then                               -- N: toward y0
                Rect(target, cx - bar / 2, y0, bar, bh / 2 + bar / 2, colour)
            end
        else
            if clearedSet[b.bx .. "," .. b.by] then
                colour = CLEARED_COLOUR[b.role] or colour
            end
            Rect(target, x0 + 1, y0 + 1, bw - 2, bh - 2, colour)
        end
    end

    for i = target.used + 1, #target.pool do
        target.pool[i]:Hide()
    end
end

-- One payload, both pictures. The panel's preview is drawn from the very table
-- the HUD draws, so the map the player picks a dungeon by and the map they
-- walk it with are the same map by construction.
local function DrawMap(m)
    -- A DIFFERENT map table is a different run, and last run's cleared blocks
    -- would paint a brand-new dungeon green until its own K arrived. The K
    -- handler re-runs this function with the cached table, so identity - not
    -- content - is the test that keeps that repaint free.
    if m ~= mapData then
        clearedSet = {}
    end
    mapData = m
    BuildMap(m, hudTarget)
    BuildMap(m, previewTarget)
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

    -- The three gate numbers describe the segment the PLAYER IS STANDING IN
    -- since WP8 (GateFieldsFor on the server side). They used to describe the
    -- next still-sealed barrier anywhere in the run, which is why the line
    -- looked stuck: the moment a gate opened it jumped to the next segment's
    -- 0/n and sat there while the party finished the room it was standing in.
    -- Which segment is measured is entirely the server's call - this only
    -- prints what arrived.
    --
    -- segPlanned 0 is the server's "there is no gate to describe here": a boss
    -- room, a player off the plan, or a worldserver from before the fields
    -- existed. The row goes blank rather than name a barrier nobody mentioned.
    -- segOpen is the server's word too - a segment whose barrier is already
    -- open still shows its kills, because they are the room's own progress.
    if r.segPlanned == 0 then
        hudGate:SetText("")
    elseif r.segOpen == 1 then
        hudGate:SetText(string.format(
            "Gate |cff00ff00open|r  |cffffffff%d/%d|r", r.segKilled, r.segPlanned))
    else
        hudGate:SetText(string.format(
            "Gate |cffffffff%d/%d|r  (%d%%)", r.segKilled, r.segPlanned, r.segPct))
    end

    -- eventSec 0 is the server's "no event is running" - nothing started, one
    -- just ended, or this worldserver does not send the pair yet. All three
    -- read the same from where the player stands: there is no defence to time,
    -- so the bar goes invisible rather than draw a clock for a fight that is
    -- not happening. Its SPACE stays (see the widgets above).
    --
    -- The minutes and seconds are split out of the server's own second count
    -- and the percent is printed as it arrived; nothing here decides how long
    -- is left or how hurt the host is. Only the two WIDTHS are clamped, and
    -- only so a bar cannot draw past its own background.
    if r.eventSec > 0 then
        if r.eventSec > eventSpan then eventSpan = r.eventSec end

        local frac = r.eventSec / eventSpan
        if frac < 0 then frac = 0 elseif frac > 1 then frac = 1 end
        local hp = r.eventPct / 100
        if hp < 0 then hp = 0 elseif hp > 1 then hp = 1 end

        hudEventFill:SetWidth(math.max(1, frac * HUD_BAR_W))
        hudEventHp:SetWidth(math.max(1, hp * HUD_BAR_W))
        hudEventBg:SetAlpha(1)
        hudEventFill:SetAlpha(1)
        hudEventHp:SetAlpha(1)
        hudEvent:SetText(string.format(
            "Hold the line |cffffffff%d:%02d|r  |cffff8800%d%%|r",
            math.floor(r.eventSec / 60), r.eventSec % 60, r.eventPct))
    else
        eventSpan = 0
        hudEventFill:SetWidth(1)
        hudEventHp:SetWidth(1)
        hudEventBg:SetAlpha(0)
        hudEventFill:SetAlpha(0)
        hudEventHp:SetAlpha(0)
        hudEvent:SetText("")
    end
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
        if m then
            -- A map on the wire is the server saying a layout exists, which is
            -- the one thing that puts the panel's preview row back up after a
            -- C reported no depths. It happens whether the panel is open or
            -- not, so opening it later shows the map already drawn.
            previewRow = true
            LayoutPanel()
            DrawMap(m)
        end
    elseif kind == "K" then
        -- The map's second half, and the only payload that repaints one. A K
        -- with no map yet is stored and paints nothing; the M that follows
        -- drops it again (see DrawMap) and brings its own K behind it, which
        -- is the order the server sends them in. A K never arrives without a
        -- plan behind it, and it never raises the preview row by itself - only
        -- a map does that.
        clearedSet = ParseCleared(body)
        if mapData then DrawMap(mapData) end
    elseif kind == "R" then
        local r = ParseRun(body)
        if r then ApplyRun(r) end
    elseif kind == "E" then
        local e = ParseEnd(body)
        if e then ApplyEnd(e) end
    elseif kind == "N" then
        -- Both frames on purpose: the raid-warning frame is the shout the
        -- player cannot miss mid-pull, the chat line is the scrollback that
        -- survives it. RaidNotice_AddMessage/RaidWarningFrame are stock
        -- 3.3.5a FrameXML (RaidWarning.lua) - no library, no fallback.
        DEFAULT_CHAT_FRAME:AddMessage("|cffFFD700The Forgotten Depths:|r " .. body)
        RaidNotice_AddMessage(RaidWarningFrame, body, ChatTypeInfo["RAID_WARNING"])
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
