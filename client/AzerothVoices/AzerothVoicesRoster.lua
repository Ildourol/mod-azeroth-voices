-- Roster selection and acknowledged, paced memory deletion.
local AzerothVoices = AzerothVoicesEventFrame
AzerothVoices.checkedBots = {}

function AzerothVoices:GetFilteredRoster(query)
    local result = {}
    query = string.lower(query or "")
    local count = table.getn(self.roster or {})
    for i = 1, count do
        local bot = self.roster[i]
        if bot and bot.name then
            if string.find(string.lower(bot.name), query, 1, true) then
                table.insert(result, bot)
            end
        end
    end
    return result
end

function AzerothVoices:UpdateRosterViews()
    local known = {}
    local count = table.getn(self.roster or {})
    for i = 1, count do
        local bot = self.roster[i]
        if bot and bot.guid then
            known[bot.guid] = true
        end
    end

    for guid in pairs(self.checkedBots) do
        if not known[guid] then
            self.checkedBots[guid] = nil
        end
    end

    if self.frame and self.frame.refreshList then
        self.frame.refreshList()
    end
end

function AzerothVoices:ConfirmForget()
    if self.forgetQueue or self.pendingRoster then
        return
    end

    local targets = {}
    local names = {}
    local count = table.getn(self.roster or {})
    for i = 1, count do
        local bot = self.roster[i]
        if bot and self.checkedBots[bot.guid] then
            table.insert(targets, bot.guid)
            if table.getn(names) < 6 then
                table.insert(names, bot.name)
            end
        end
    end

    local targetCount = table.getn(targets)
    if targetCount == 0 then
        return
    end

    local description = table.concat(names, ", ")
    local nameCount = table.getn(names)
    if targetCount > nameCount then
        description = description .. " and " .. (targetCount - nameCount) .. " more"
    end

    StaticPopupDialogs["AZEROTHVOICES_CONFIRM_FORGET"].targetData = targets
    local popup = StaticPopup_Show("AZEROTHVOICES_CONFIRM_FORGET", targetCount, description)
    if popup then
        popup.data = targets
    end
end

function AzerothVoices:StartForgetBatch(targets)
    if self.forgetQueue or not targets or table.getn(targets) == 0 then
        return
    end

    self:StopTonePoll()
    self:StopBackstoryPoll()
    self.pendingProfileGuid = nil
    self.loadedTraits = nil
    self:SetSaveEnabled(false)
    self:SetRegenStoryEnabled(false)

    self.forgetQueue = targets
    self.forgetIndex = 1
    self.forgetDone = 0
    self.forgetElapsed = 0
    self.forgetWaiting = nil
    self:UpdateRosterViews()
    self:SetStatus("Forgetting selected bots...", 1, 0.82, 0)
end

function AzerothVoices:HandleForgetQueue(elapsed)
    if not self.forgetQueue then
        return
    end

    self.forgetElapsed = self.forgetElapsed + elapsed
    if self.forgetWaiting then
        if self.forgetElapsed >= 10 then
            self:FinishForgetBatch("No server reply. Use Refresh before retrying.")
        end
        return
    end

    if self.forgetElapsed < 0.3 then
        return
    end

    self.forgetElapsed = 0
    self.forgetWaiting = self.forgetQueue[self.forgetIndex]
    self:SendCommand("forget " .. self.forgetWaiting)
end

function AzerothVoices:HandleForgotten(guid)
    if not guid then
        return
    end

    -- Duplicate or delayed acknowledgements must not advance the batch
    if self.forgetQueue and guid ~= self.forgetWaiting then
        return
    end

    local rosterCount = table.getn(self.roster or {})
    for i = rosterCount, 1, -1 do
        if self.roster[i] and self.roster[i].guid == guid then
            table.remove(self.roster, i)
        end
    end

    self.checkedBots[guid] = nil
    if self.selectedGuid == guid then
        self.selectedGuid = nil
        if AzerothVoicesDB then
            AzerothVoicesDB.selectedGuid = nil
        end
        self.loadedTraits = nil
        self.pendingProfileGuid = nil
        local empty = {
            trait1 = "", trait2 = "", trait3 = "",
            tone = "", backstory = ""
        }
        self:ApplyProfileToPanel(self.frame, empty)
    end

    if not self.forgetQueue then
        self:RequestRoster()
        return
    end

    self.forgetDone = self.forgetDone + 1
    self.forgetIndex = self.forgetIndex + 1
    self.forgetWaiting = nil
    self.forgetElapsed = 0
    self:UpdateRosterViews()

    local queueCount = table.getn(self.forgetQueue)
    if self.forgetIndex > queueCount then
        self:FinishForgetBatch()
    else
        self:SetStatus("Forgot " .. self.forgetDone .. "/" .. queueCount .. " bots...", 1, 0.82, 0)
    end
end

function AzerothVoices:FinishForgetBatch(reason)
    local done = self.forgetDone or 0
    self.forgetQueue = nil
    self.forgetWaiting = nil
    self:UpdateRosterViews()
    self:RequestRoster()

    local message = "Forgot " .. done .. " bot(s)."
    if reason then
        message = message .. " " .. reason
    end
    self:SetStatus(
        message,
        reason and 1 or 0.3,
        reason and 0.4 or 1,
        0.3
    )
    if DEFAULT_CHAT_FRAME then
        DEFAULT_CHAT_FRAME:AddMessage("|cffffcc00Azeroth Voices:|r " .. message)
    end
end

StaticPopupDialogs["AZEROTHVOICES_CONFIRM_FORGET"] = {
    text = "Forget %d bot(s)?\n%s\n\nTheir memories with you will be erased."
        .. " Their personalities are preserved.",
    button1 = "Forget",
    button2 = "Cancel",
    OnAccept = function()
        local data = nil
        if this and this.GetParent then
            local p = this:GetParent()
            if p then
                data = p.data
            end
        end
        if not data then
            data = StaticPopupDialogs["AZEROTHVOICES_CONFIRM_FORGET"].targetData
        end
        if data then
            AzerothVoices:StartForgetBatch(data)
        end
    end,
    timeout = 0,
    whileDead = false,
    hideOnEscape = true,
}
