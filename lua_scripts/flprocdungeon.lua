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
    -- watching for the change is what makes the report reach the server
    -- without the DLL having to announce itself.
    SendVersionIfChanged()

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
        lastSentVersion = nil          -- re-report after every loading screen
        SendVersionIfChanged()
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
