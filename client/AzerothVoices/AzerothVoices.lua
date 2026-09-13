local AzerothVoices = CreateFrame("Frame", "AzerothVoicesEventFrame")
AzerothVoicesEventFrame = AzerothVoices

AzerothVoices.prefix = "AZEROTH_VOICES"
AzerothVoices.roster = {}
AzerothVoices.pendingRoster = nil
AzerothVoices.selectedGuid = nil
AzerothVoices.pendingProfileGuid = nil
AzerothVoices.pendingToneGuid = nil
AzerothVoices.tonePollElapsed = 0
AzerothVoices.tonePollRemaining = 0
AzerothVoices.pendingBackstoryGuid = nil
AzerothVoices.backstoryPollElapsed = 0
AzerothVoices.backstoryPollRemaining = 0
AzerothVoices.requestId = 0
AzerothVoices.inboundBuffers = {}

local function trim(value)
    if not value then
        return ""
    end
    local _, _, content = string.find(value, "^%s*(.-)%s*$")
    return content or ""
end

local function sanitizeInput(value)
    value = trim(value or "")
    local clean = ""
    for i = 1, string.len(value) do
        local b = string.byte(value, i)
        if b < 32 then
            clean = clean .. " "
        else
            clean = clean .. string.sub(value, i, i)
        end
    end
    local _, _, collapsed = string.find(clean, "^%s*(.-)%s*$")
    local result = ""
    local inSpace = false
    local len = string.len(collapsed or "")
    for i = 1, len do
        local ch = string.sub(collapsed, i, i)
        if ch == " " then
            if not inSpace then
                result = result .. " "
                inSpace = true
            end
        else
            result = result .. ch
            inSpace = false
        end
    end
    return trim(result)
end

function AzerothVoices:Encode(value)
    value = sanitizeInput(value)
    if value == "" then
        return "-"
    end

    return (string.gsub(value, "([^%w%-_%.~])", function(char)
        return string.format("%%%02X", string.byte(char))
    end))
end

function AzerothVoices:Decode(value)
    if not value or value == "-" then
        return ""
    end

    return (string.gsub(value, "%%(%x%x)", function(hex)
        return string.char(tonumber(hex, 16))
    end))
end

function AzerothVoices:SetStatus(text, r, g, b)
    if self.frame and self.frame.status then
        self.frame.status:SetText(text or "")
        self.frame.status:SetTextColor(
            r or 1, g or 0.82, b or 0
        )
    end
end

function AzerothVoices:SendCommand(command)
    self.requestId = math.mod((self.requestId or 0) + 1, 1000000)
    local reqId = tostring(self.requestId)
    local len = string.len(command)
    local chunkSize = 180
    local partCount = math.floor((len + chunkSize - 1) / chunkSize)
    if partCount < 1 then
        partCount = 1
    end

    for partIdx = 1, partCount do
        local startPos = (partIdx - 1) * chunkSize + 1
        local endPos = math.min(len, partIdx * chunkSize)
        local chunk = ""
        if len > 0 then
            chunk = string.sub(command, startPos, endPos)
        end
        local line = ".avaddon 1 " .. reqId .. " " .. partIdx .. " " .. partCount .. " " .. chunk
        SendChatMessage(line, "SAY")
    end
end

function AzerothVoices:StopTonePoll()
    self.pendingToneGuid = nil
    self.tonePollElapsed = 0
    self.tonePollRemaining = 0
end

function AzerothVoices:StartTonePoll(guid)
    guid = tonumber(guid)
    if not guid then
        return
    end

    self.pendingToneGuid = guid
    self.tonePollElapsed = 0
    self.tonePollRemaining = 120

    if self.frame and self.frame.tone then
        self.frame.tone:SetTextColor(0.5, 0.5, 0.5)
        self.frame.tone:SetText("Generating tone...")
    end
end

function AzerothVoices:HandleTonePoll(elapsed)
    if not self.pendingToneGuid then
        return
    end

    self.tonePollElapsed = self.tonePollElapsed + elapsed
    self.tonePollRemaining = self.tonePollRemaining - elapsed

    if self.tonePollRemaining <= 0 then
        self:SetStatus(
            "Tone generation is still pending. Use Refresh.",
            1, 0.82, 0
        )
        self:StopTonePoll()
        return
    end

    if self.tonePollElapsed >= 1.5 then
        self.tonePollElapsed = 0
        self:SendCommand("get " .. self.pendingToneGuid)
    end
end

function AzerothVoices:SetRegenStoryEnabled(enabled)
    if self.frame and self.frame.regenStoryBtn then
        if enabled then
            self.frame.regenStoryBtn:Enable()
        else
            self.frame.regenStoryBtn:Disable()
        end
    end
end

function AzerothVoices:SetSaveEnabled(enabled)
    if self.frame and self.frame.saveBtn then
        if enabled then
            self.frame.saveBtn:Enable()
        else
            self.frame.saveBtn:Disable()
        end
    end
end

function AzerothVoices:UpdateSaveButton()
    local loaded = self.loadedTraits
    if not loaded or self.pendingProfileGuid or self.forgetQueue then
        self:SetSaveEnabled(false)
        return
    end
    local p = self.frame
    if not p then
        self:SetSaveEnabled(false)
        return
    end
    local t1 = sanitizeInput(
        p.trait1 and p.trait1:GetText() or ""
    )
    local t2 = sanitizeInput(
        p.trait2 and p.trait2:GetText() or ""
    )
    local t3 = sanitizeInput(
        p.trait3 and p.trait3:GetText() or ""
    )
    local changed = (
        t1 ~= (loaded.trait1 or "")
        or t2 ~= (loaded.trait2 or "")
        or t3 ~= (loaded.trait3 or "")
    )
    self:SetSaveEnabled(changed)
end

function AzerothVoices:StopBackstoryPoll()
    self.pendingBackstoryGuid = nil
    self.backstoryPollElapsed = 0
    self.backstoryPollRemaining = 0
    self:SetRegenStoryEnabled(true)
end

function AzerothVoices:StartBackstoryPoll(guid)
    guid = tonumber(guid)
    if not guid then
        return
    end

    self.pendingBackstoryGuid = guid
    self.backstoryPollElapsed = 0
    self.backstoryPollRemaining = 120

    self:SetRegenStoryEnabled(false)

    local placeholder = "Creating background story..."
    if self.frame and self.frame.backstory then
        self.frame.backstory:SetText(placeholder)
        self.frame.backstory:SetTextColor(
            0.5, 0.5, 0.5
        )
    end
end

function AzerothVoices:HandleBackstoryPoll(elapsed)
    if not self.pendingBackstoryGuid then
        return
    end

    self.backstoryPollElapsed =
        self.backstoryPollElapsed + elapsed
    self.backstoryPollRemaining =
        self.backstoryPollRemaining - elapsed

    if self.backstoryPollRemaining <= 0 then
        self:SetStatus(
            "Backstory generation still pending."
            .. " Use Refresh.",
            1, 0.82, 0
        )
        self:StopBackstoryPoll()
        return
    end

    if self.backstoryPollElapsed >= 2.0 then
        self.backstoryPollElapsed = 0
        self:SendCommand(
            "get " .. self.pendingBackstoryGuid
        )
    end
end

function AzerothVoices:MigrateSavedVariables()
    AzerothVoicesDB = AzerothVoicesDB or {}
    if ChatterDB and not AzerothVoicesDB.migratedFromChatter then
        if ChatterDB.point then
            AzerothVoicesDB.point = ChatterDB.point
        end
        if ChatterDB.relPoint then
            AzerothVoicesDB.relPoint = ChatterDB.relPoint
        end
        if ChatterDB.x then
            AzerothVoicesDB.x = ChatterDB.x
        end
        if ChatterDB.y then
            AzerothVoicesDB.y = ChatterDB.y
        end
        if ChatterDB.selectedGuid then
            AzerothVoicesDB.selectedGuid = ChatterDB.selectedGuid
        end
        AzerothVoicesDB.migratedFromChatter = true
    end
end

function AzerothVoices:SaveWindowPosition()
    if not self.frame then
        return
    end

    local point, _, relPoint, x, y =
        self.frame:GetPoint(1)
    AzerothVoicesDB = AzerothVoicesDB or {}
    AzerothVoicesDB.point = point
    AzerothVoicesDB.relPoint = relPoint
    AzerothVoicesDB.x = x
    AzerothVoicesDB.y = y
end

function AzerothVoices:RestoreWindowPosition()
    if not self.frame then
        return
    end

    self.frame:ClearAllPoints()
    if AzerothVoicesDB and AzerothVoicesDB.point then
        self.frame:SetPoint(
            AzerothVoicesDB.point,
            UIParent,
            AzerothVoicesDB.relPoint or AzerothVoicesDB.point,
            AzerothVoicesDB.x or 0,
            AzerothVoicesDB.y or 0
        )
    else
        self.frame:SetPoint("CENTER", UIParent, "CENTER", 0, 0)
    end
end

function AzerothVoices:ApplyProfileToPanel(p, profile)
    if not p then
        return
    end
    if p.trait1 then
        p.trait1:SetText(profile.trait1 or "")
    end
    if p.trait2 then
        p.trait2:SetText(profile.trait2 or "")
    end
    if p.trait3 then
        p.trait3:SetText(profile.trait3 or "")
    end
    if p.tone then
        local toneText = profile.tone or ""
        local polling = (
            self.pendingToneGuid
            and self.pendingToneGuid == profile.guid
        )
        if not polling or toneText ~= "" then
            p.tone:SetText(toneText)
            p.tone:SetTextColor(0.8, 0.8, 0.8)
        end
    end
    if p.backstory then
        local bsText = profile.backstory or ""
        local polling = (
            self.pendingBackstoryGuid
            and self.pendingBackstoryGuid == profile.guid
        )
        if not polling or bsText ~= "" then
            p.backstory:SetText(bsText)
            p.backstory:SetTextColor(0.8, 0.8, 0.8)
        end
    end
end

function AzerothVoices:ApplyProfile(profile)
    if profile.guid ~= self.selectedGuid or self.forgetQueue then
        return
    end
    self.pendingProfileGuid = nil
    self:SetRegenStoryEnabled(not self.pendingBackstoryGuid)
    local awaitingTone = (
        self.pendingToneGuid == profile.guid
    )
    self.selectedGuid = profile.guid

    self.loadedTraits = {
        trait1 = profile.trait1 or "",
        trait2 = profile.trait2 or "",
        trait3 = profile.trait3 or "",
    }

    self:ApplyProfileToPanel(self.frame, profile)
    self:SetSaveEnabled(false)

    AzerothVoicesDB = AzerothVoicesDB or {}
    AzerothVoicesDB.selectedGuid = profile.guid

    self:UpdateRosterViews()
    if awaitingTone then
        if profile.tone and profile.tone ~= "" then
            self:StopTonePoll()
            self:SetStatus(
                "Generated tone for "
                    .. (profile.name or "bot"),
                0.3, 1, 0.3
            )
        else
            self:SetStatus(
                "Generating tone...", 1, 0.82, 0
            )
        end
    else
        self:SetStatus(
            "Loaded " .. (profile.name or "bot"),
            0.3, 1, 0.3
        )
    end
end

function AzerothVoices:SelectBot(guid)
    if self.forgetQueue then return end
    guid = tonumber(guid)
    if not guid then
        return
    end

    if self.pendingToneGuid and self.pendingToneGuid ~= guid then
        self:StopTonePoll()
    end
    if self.pendingBackstoryGuid
        and self.pendingBackstoryGuid ~= guid then
        self:StopBackstoryPoll()
    end

    self.selectedGuid = guid
    self.pendingProfileGuid = guid
    self.loadedTraits = nil
    self:SetSaveEnabled(false)
    self:SetRegenStoryEnabled(false)
    local empty = {
        trait1 = "", trait2 = "", trait3 = "",
        tone = "", backstory = ""
    }
    self:ApplyProfileToPanel(self.frame, empty)
    self:UpdateRosterViews()
    self:SetStatus("Loading bot profile...", 1, 0.82, 0)
    self:SendCommand("get " .. guid)
end

function AzerothVoices:RequestRoster()
    if self.forgetQueue or self.pendingRoster then return end
    self.pendingRoster = {}
    self.rosterElapsed = 0
    self:UpdateRosterViews()
    self:SetStatus("Requesting roster...", 1, 0.82, 0)
    self:SendCommand("roster")
end

function AzerothVoices:SaveProfile()
    if self.pendingProfileGuid or self.forgetQueue then return end
    if not self.selectedGuid then
        self:SetStatus("Select a bot first.", 1, 0.2, 0.2)
        return
    end

    local p = self.frame
    if not p then
        self:SetStatus("No panel open.", 1, 0.2, 0.2)
        return
    end

    local trait1 = sanitizeInput(p.trait1:GetText())
    local trait2 = sanitizeInput(p.trait2:GetText())
    local trait3 = sanitizeInput(p.trait3:GetText())

    p.trait1:SetText(trait1)
    p.trait2:SetText(trait2)
    p.trait3:SetText(trait3)

    if trait1 == "" or trait2 == "" or trait3 == "" then
        self:SetStatus("All three traits are required.", 1, 0.2, 0.2)
        return
    end

    if string.len(trait1) > 64 or string.len(trait2) > 64
        or string.len(trait3) > 64 then
        self:SetStatus("Traits must stay under 64 characters.", 1, 0.2, 0.2)
        return
    end

    self.pendingTraits = {
        guid = self.selectedGuid,
        trait1 = trait1,
        trait2 = trait2,
        trait3 = trait3,
    }

    local loaded = self.loadedTraits or {}
    local changed = (
        trait1 ~= (loaded.trait1 or "")
        or trait2 ~= (loaded.trait2 or "")
        or trait3 ~= (loaded.trait3 or "")
    )

    if changed then
        StaticPopup_Show(
            "AZEROTHVOICES_CONFIRM_SAVE_TRAITS"
        )
    else
        self:SetStatus(
            "Traits unchanged.", 0.3, 1, 0.3
        )
    end
end

function AzerothVoices:DoSaveProfile()
    local t = self.pendingTraits
    if not t or t.guid ~= self.selectedGuid
        or self.pendingProfileGuid or self.forgetQueue then
        return
    end

    local guid = self.selectedGuid
    self:StopTonePoll()
    self:StopBackstoryPoll()
    self:SendCommand(
        "set " .. guid .. " " ..
        self:Encode(t.trait1) .. " " ..
        self:Encode(t.trait2) .. " " ..
        self:Encode(t.trait3)
    )
    self:StartTonePoll(guid)
    self:StartBackstoryPoll(guid)
    self:SetStatus(
        "Saving traits and regenerating tone"
            .. " and backstory...",
        1, 0.82, 0
    )
    self.pendingTraits = nil
end

function AzerothVoices:GetSelectedName()
    if not self.selectedGuid then
        return nil
    end
    local guid = tonumber(self.selectedGuid)
    if not guid then return nil end
    local count = table.getn(self.roster or {})
    for i = 1, count do
        local entry = self.roster[i]
        if entry and entry.guid == guid then
            return entry.name
        end
    end
    return nil
end

function AzerothVoices:RegenBackstory()
    if self.pendingProfileGuid or self.forgetQueue then return end
    if not self.selectedGuid then
        self:SetStatus("Select a bot first.", 1, 0.2, 0.2)
        return
    end

    local guid = self.selectedGuid
    self:StopBackstoryPoll()
    self:SendCommand("regenbackstory " .. guid)
    self:StartBackstoryPoll(guid)
    self:SetStatus(
        "Regenerating backstory...", 1, 0.82, 0
    )
end

function AzerothVoices:Toggle()
    self:BuildFrame()

    if self.frame:IsShown() then
        self.frame:Hide()
        return
    end

    self.frame:Show()
    self.frame:Raise()
    self:RequestRoster()
end

function AzerothVoices:HandleRosterEntry(guidToken, nameToken)
    local guid = tonumber(guidToken)
    local name = self:Decode(nameToken)
    if not guid or name == "" then
        return
    end

    table.insert(self.pendingRoster, {
        guid = guid,
        name = name
    })
end

function AzerothVoices:FinishRoster()
    self.roster = self.pendingRoster or {}
    self.pendingRoster = nil

    table.sort(self.roster, function(a, b)
        return string.lower(a.name) < string.lower(b.name)
    end)

    self:UpdateRosterViews()

    if table.getn(self.roster) == 0 then
        self.selectedGuid = nil
        self.pendingProfileGuid = nil
        self.loadedTraits = nil
        AzerothVoicesDB = AzerothVoicesDB or {}
        AzerothVoicesDB.selectedGuid = nil
        self:StopTonePoll()
        self:StopBackstoryPoll()
        self:SetSaveEnabled(false)
        self:SetRegenStoryEnabled(false)
        self:UpdateRosterViews()
        local empty = {
            trait1 = "", trait2 = "", trait3 = "",
            tone = "", backstory = "",
        }
        self:ApplyProfileToPanel(self.frame, empty)
        self:SetStatus("No known bots yet.", 1, 0.82, 0)
        return
    end

    local preferredGuid = self.selectedGuid
    if not preferredGuid and AzerothVoicesDB then
        preferredGuid = AzerothVoicesDB.selectedGuid
    end

    local found = nil
    local count = table.getn(self.roster)
    for i = 1, count do
        local bot = self.roster[i]
        if bot and bot.guid == preferredGuid then
            found = bot.guid
            break
        end
    end

    if not found then
        found = self.roster[1].guid
    end

    self:SelectBot(found)
end

function AzerothVoices:HandleProfilePayload(rest)
    local _, _, guid, name, trait1, trait2, trait3, tone =
        string.find(
            rest,
            "^(%d+)%s+(%S+)%s+(%S+)%s+(%S+)%s+(%S+)%s+(%S+)$"
        )
    if not guid then
        return
    end

    self:ApplyProfile({
        guid = tonumber(guid),
        name = self:Decode(name),
        trait1 = self:Decode(trait1),
        trait2 = self:Decode(trait2),
        trait3 = self:Decode(trait3),
        tone = self:Decode(tone),
    })
end

function AzerothVoices:HandleBackstoryPayload(rest)
    local _, _, guid, encoded = string.find(
        rest, "^(%d+)%s+(.*)$"
    )
    if not guid then
        return
    end

    local numGuid = tonumber(guid)
    local text = self:Decode(encoded or "-")

    if text and text ~= ""
        and self.selectedGuid == numGuid then
        if self.frame and self.frame.backstory then
            self.frame.backstory:SetText(text)
            self.frame.backstory:SetTextColor(
                0.8, 0.8, 0.8
            )
        end
    end

    if self.pendingBackstoryGuid == numGuid then
        if text and text ~= "" then
            self:StopBackstoryPoll()
            if not self.pendingToneGuid then
                self:SetStatus(
                    "Backstory generated.",
                    0.3, 1, 0.3
                )
            end
        end
    end
end

function AzerothVoices:HandleUpdatedPayload(rest)
    local _, _, guid, name, flag = string.find(
        rest, "^(%d+)%s+(%S+)%s+(%S+)$"
    )
    if guid and name and tonumber(guid) == self.selectedGuid
        and not self.forgetQueue then
        self.selectedGuid = tonumber(guid)
        local changed = (flag == "changed")
        if changed then
            self:StartTonePoll(self.selectedGuid)
            self:StartBackstoryPoll(self.selectedGuid)
            self:SetStatus(
                "Saved. Regenerating tone"
                    .. " and backstory...",
                1, 0.82, 0
            )
        else
            self:StopTonePoll()
            self:StopBackstoryPoll()
            self:SetStatus(
                "Traits saved for "
                    .. self:Decode(name) .. ".",
                0.3, 1, 0.3
            )
            self:SendCommand(
                "get " .. tonumber(guid)
            )
        end
    end
end

function AzerothVoices:HandleBackstoryRegenPayload(rest)
    local _, _, guid, name = string.find(
        rest, "^(%d+)%s+(%S+)$"
    )
    if guid and name then
        self:SetStatus(
            "Regenerating backstory for "
                .. self:Decode(name) .. "...",
            1, 0.82, 0
        )
    end
end

function AzerothVoices:HandleForgottenPayload(rest)
    local _, _, guid = string.find(rest, "^(%d+)")
    if guid then
        self:HandleForgotten(tonumber(guid))
    end
end

function AzerothVoices:HandleErrorPayload(rest)
    if self.forgetQueue then
        local _, _, errText = string.find(rest, "^%S+%s*(.*)$")
        self:FinishForgetBatch("Stopped: " .. self:Decode(errText or rest))
        return
    end
    self.pendingRoster = nil
    local _, _, _, encoded = string.find(
        rest, "^(%S+)%s*(.*)$"
    )
    self:SetStatus(
        self:Decode(encoded or rest), 1, 0.2, 0.2
    )
end

function AzerothVoices:HandlePayload(payload)
    local _, _, command, rest = string.find(payload, "^(%S+)%s*(.*)$")
    if not command then
        return
    end

    if command == "ROSTER_BEGIN" then
        self.pendingRoster = {}
        return
    end

    if command == "ROSTER" then
        local _, _, guid, name = string.find(rest, "^(%d+)%s+(%S+)$")
        if guid and name and self.pendingRoster then
            self:HandleRosterEntry(guid, name)
        end
        return
    end

    if command == "ROSTER_END" then
        self:FinishRoster()
        return
    end

    if command == "PROFILE" then
        self:HandleProfilePayload(rest)
        return
    end

    if command == "BACKSTORY" then
        self:HandleBackstoryPayload(rest)
        return
    end

    if command == "UPDATED" then
        self:HandleUpdatedPayload(rest)
        return
    end

    if command == "BACKSTORY_REGEN" then
        self:HandleBackstoryRegenPayload(rest)
        return
    end

    if command == "FORGOTTEN" then
        self:HandleForgottenPayload(rest)
        return
    end

    if command == "ERROR" then
        self:HandleErrorPayload(rest)
    end
end

function AzerothVoices:HandleAddonMessage(prefix, message, channel, sender)
    if prefix ~= self.prefix or channel ~= "GUILD" or sender ~= UnitName("player") then
        return
    end

    local _, _, version, reqId, msgIdxStr, partIdxStr, partCountStr, chunk =
        string.find(message, "^(%d+)\t([^\t]+)\t(%d+)\t(%d+)\t(%d+)\t?(.*)$")

    if version ~= "1" or not reqId or not msgIdxStr or not partIdxStr or not partCountStr then
        return
    end

    local msgIdx = tonumber(msgIdxStr)
    local partIdx = tonumber(partIdxStr)
    local partCount = tonumber(partCountStr)

    if not msgIdx or not partIdx or not partCount or
        msgIdx < 1 or partIdx < 1 or partCount < 1 or partIdx > partCount or partCount > 32 then
        return
    end

    local bufferKey = reqId .. ":" .. msgIdx
    local now = GetTime and GetTime() or 0

    -- Expire stale buffers older than 10 seconds
    for key, buf in pairs(self.inboundBuffers) do
        if buf.timestamp and (now - buf.timestamp > 10) then
            self.inboundBuffers[key] = nil
        end
    end

    local buf = self.inboundBuffers[bufferKey]
    if not buf or buf.partCount ~= partCount then
        buf = {
            partCount = partCount,
            parts = {},
            receivedCount = 0,
            totalBytes = 0,
            timestamp = now,
        }
        self.inboundBuffers[bufferKey] = buf
    end

    chunk = chunk or ""
    if not buf.parts[partIdx] then
        if buf.totalBytes + string.len(chunk) > 16384 then
            self.inboundBuffers[bufferKey] = nil
            return
        end
        buf.parts[partIdx] = chunk
        buf.receivedCount = buf.receivedCount + 1
        buf.totalBytes = buf.totalBytes + string.len(chunk)
    end

    if buf.receivedCount >= partCount then
        local payload = ""
        for i = 1, partCount do
            payload = payload .. (buf.parts[i] or "")
        end
        self.inboundBuffers[bufferKey] = nil
        self:HandlePayload(payload)
    end
end

StaticPopupDialogs["AZEROTHVOICES_CONFIRM_SAVE_TRAITS"] = {
    text = "These traits are different from the saved ones. Saving will regenerate this bot's tone and background story. Continue?",
    button1 = "Save",
    button2 = "Cancel",
    OnAccept = function()
        AzerothVoices:DoSaveProfile()
    end,
    timeout = 0,
    whileDead = false,
    hideOnEscape = true,
}

SLASH_AZEROTHVOICES1 = "/azerothvoices"
SLASH_AZEROTHVOICES2 = "/avvoices"
SLASH_AZEROTHVOICES3 = "/chatter"
SLASH_AZEROTHVOICES4 = "/llmc"
SlashCmdList["AZEROTHVOICES"] = function()
    AzerothVoices:Toggle()
end

AzerothVoices:SetScript("OnUpdate", function()
    local self = this
    local elapsed = arg1 or 0
    if self.pendingRoster then
        self.rosterElapsed = (self.rosterElapsed or 0) + elapsed
        if self.rosterElapsed >= 10 then
            self.pendingRoster = nil
            self:UpdateRosterViews()
            self:SetStatus("Roster request timed out. Use Refresh.", 1, 0.4, 0.3)
        end
    end
    self:HandleForgetQueue(elapsed)
    self:HandleTonePoll(elapsed)
    self:HandleBackstoryPoll(elapsed)
end)

AzerothVoices:SetScript("OnEvent", function()
    local self = this
    local currentEvent = event
    if currentEvent == "PLAYER_LOGIN" then
        self:MigrateSavedVariables()
        self:BuildFrame()
    elseif currentEvent == "PLAYER_LOGOUT" then
        self:SaveWindowPosition()
    elseif currentEvent == "CHAT_MSG_ADDON" then
        self:HandleAddonMessage(arg1, arg2, arg3, arg4)
    end
end)

AzerothVoices:RegisterEvent("PLAYER_LOGIN")
AzerothVoices:RegisterEvent("PLAYER_LOGOUT")
AzerothVoices:RegisterEvent("CHAT_MSG_ADDON")
