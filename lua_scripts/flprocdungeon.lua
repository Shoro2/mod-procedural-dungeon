-- FLProcDungeon - the client half of the PDv2 client link.
--
-- Delivered to the client by AIO (this one file serves both sides: on the
-- server AIO.AddAddon() registers it for delivery and returns true, so the
-- client code below never runs server-side).
--
-- This addon holds NO protocol logic on purpose - it is a dumb relay between
-- the server's addon channel and the injected FLStream.dll, and every decision
-- lives in C++ (PDClientLink). The lesson behind that rule is the
-- dungeon-challenge affix display, where a Lua copy of a C++ value drifted.
--
--   server whisper  FLPDS\tM<manifest>  -> RunScript long comment to the DLL
--   DLL globals     FLPD_DLL_VERSION / FLPD_ACK (set by every DLL reply)
--   relay upstream  SendAddonMessage("FLPD", "VER <n>" | "ACK <text>")

local AIO = AIO or require("AIO")
if AIO.AddAddon() then
    return
end

local PREFIX_DOWN = "FLPDS"   -- server -> client
local PREFIX_UP = "FLPD"      -- client -> server

local lastSentVersion = nil   -- last VER value relayed upstream
local lastSeenAck = nil       -- last FLPD_ACK value relayed upstream
local pollUntil = 0           -- FLPD_ACK is watched while GetTime() < pollUntil
local reReportAt = 0          -- one-shot VER re-report after the loading screen
local probeAt = 0             -- next DLL wake probe while no version is known
local throttle = 0

local function SendUp(text)
    SendAddonMessage(PREFIX_UP, text, "WHISPER", UnitName("player"))
end

local function SendVersionIfChanged()
    local version = _G.FLPD_DLL_VERSION or 0
    if version ~= lastSentVersion then
        lastSentVersion = version
        SendUp("VER " .. tostring(version))
    end
end

local frame = CreateFrame("Frame")
frame:RegisterEvent("PLAYER_ENTERING_WORLD")
frame:RegisterEvent("CHAT_MSG_ADDON")

frame:SetScript("OnUpdate", function(self, elapsed)
    throttle = throttle + elapsed
    if throttle < 0.2 then
        return
    end
    throttle = 0

    -- The DLL is injected AFTER login, so its version global appears late;
    -- watching for the change is what makes the report reach the server.
    SendVersionIfChanged()

    -- The DLL announces its version on the first EXECUTED BUFFER after
    -- injection - but the client runs no buffer by itself in idle play
    -- (measured 2026-08-06: injected, armed, silent). So while no version is
    -- known, run one comment-only buffer per second: a no-op for the game,
    -- the wake-up call for a freshly injected DLL. Stops by itself the
    -- moment a version appears.
    local ver = _G.FLPD_DLL_VERSION
    if (not ver or ver == 0) and GetTime() >= probeAt then
        probeAt = GetTime() + 1
        RunScript("-- flpd wake")
    end

    -- One retry, and only while the DLL has never answered: the server's
    -- push can die on the login boundary (client-to-server addon chat
    -- survives the loading screen, the reverse does not - measured
    -- 2026-08-06). Once an ACK exists the link is up and re-reporting would
    -- only make the server recompose, which is exactly what must not happen.
    if reReportAt > 0 and GetTime() >= reReportAt then
        reReportAt = 0
        if not _G.FLPD_ACK then
            lastSentVersion = nil
            SendVersionIfChanged()
        end
    end

    if GetTime() < pollUntil then
        local ack = _G.FLPD_ACK
        if ack and ack ~= lastSeenAck then
            lastSeenAck = ack
            SendUp("ACK " .. tostring(ack))
            if string.find(ack, "^READY") or string.find(ack, "^NAK") then
                pollUntil = 0
            end
        end
    end
end)

frame:SetScript("OnEvent", function(self, event, arg1, arg2)
    if event == "PLAYER_ENTERING_WORLD" then
        -- Deliberately NOT re-reporting here. This event also fires for the
        -- dungeon's own loading screen, and a version report makes the server
        -- invalidate and push - which makes the DLL recompose and switch the
        -- slot the client is being served from, killing it mid-map
        -- (measured 2026-08-06). The report below happens once per Lua state,
        -- which is exactly the lifetime that can lose the DLL's globals.
        SendVersionIfChanged()
        reReportAt = GetTime() + 5
    elseif event == "CHAT_MSG_ADDON" then
        if arg1 ~= PREFIX_DOWN then
            return                     -- also skips the echo of our own FLPD whispers
        end
        local kind = string.sub(arg2, 1, 1)
        if kind == "M" then
            local manifest = string.sub(arg2, 2)
            -- Baseline before the feed, so only the RESULTING ack change is
            -- relayed. The manifest already ends in a newline, which the
            -- long-comment framing requires before its closing brackets.
            lastSeenAck = _G.FLPD_ACK
            RunScript("--[[FLPD:MANIFEST\n" .. manifest .. "]]")
            pollUntil = GetTime() + 10
        end
    end
end)
