-- Single-window responsive editor for Turtle WoW 1.18.1 / Vanilla Interface 11200.
local AzerothVoices = AzerothVoicesEventFrame

local function button(parent, text, width, callback, height)
    local control = CreateFrame("Button", nil, parent, "UIPanelButtonTemplate")
    control:SetWidth(width)
    control:SetHeight(height or 22)
    control:SetText(text)
    control:SetScript("OnClick", function()
        if callback then
            callback()
        end
    end)
    return control
end

local function slider(parent)
    local control = CreateFrame("Slider", nil, parent)
    control:SetWidth(12)
    control:SetOrientation("VERTICAL")
    control:SetMinMaxValues(0, 0)
    control:SetValueStep(1)
    control:SetValue(0)
    control:SetThumbTexture("Interface\\Buttons\\UI-ScrollBar-Knob")
    control:SetBackdrop({
        bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
        edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
        tile = true,
        tileSize = 8,
        edgeSize = 8,
        insets = { left = 1, right = 1, top = 1, bottom = 1 }
    })
    control:SetBackdropColor(0.1, 0.1, 0.1, 0.6)
    return control
end

local function createLabel(parent, text, x, y)
    local label = parent:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    label:SetPoint("TOPLEFT", parent, "TOPLEFT", x, y)
    label:SetJustifyH("LEFT")
    label:SetText(text)
    return label
end

local function createTraitBox(parent, name, x, y, width)
    local box = CreateFrame("EditBox", name, parent, "InputBoxTemplate")
    box:SetPoint("TOPLEFT", parent, "TOPLEFT", x, y)
    box:SetWidth(width)
    box:SetHeight(20)
    box:SetAutoFocus(false)
    box:SetMaxLetters(64)
    box:SetFontObject(GameFontHighlight)
    return box
end

local function createToneBox(parent, x, y, width, height)
    local holder = CreateFrame("Frame", nil, parent)
    holder:SetPoint("TOPLEFT", parent, "TOPLEFT", x, y)
    holder:SetWidth(width)
    holder:SetHeight(height)
    holder:SetBackdrop({
        bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
        edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
        tile = true,
        tileSize = 12,
        edgeSize = 12,
        insets = { left = 3, right = 3, top = 3, bottom = 3 }
    })
    holder:SetBackdropColor(0, 0, 0, 0.8)
    holder:SetBackdropBorderColor(0.5, 0.5, 0.5, 0.8)

    local text = holder:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    text:SetPoint("TOPLEFT", holder, "TOPLEFT", 6, -6)
    text:SetPoint("BOTTOMRIGHT", holder, "BOTTOMRIGHT", -6, 6)
    text:SetJustifyH("LEFT")
    text:SetJustifyV("TOP")
    holder.text = text

    function holder:SetText(val)
        self.text:SetText(val or "")
    end
    function holder:GetText()
        return self.text:GetText() or ""
    end
    function holder:SetTextColor(r, g, b)
        self.text:SetTextColor(r, g, b)
    end

    return holder
end

local function createBackstoryBox(parent, x, y, width, height)
    local holder = CreateFrame("Frame", nil, parent)
    holder:SetPoint("TOPLEFT", parent, "TOPLEFT", x, y)
    holder:SetWidth(width)
    holder:SetHeight(height)
    holder:SetBackdrop({
        bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
        edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
        tile = true,
        tileSize = 12,
        edgeSize = 12,
        insets = { left = 3, right = 3, top = 3, bottom = 3 }
    })
    holder:SetBackdropColor(0, 0, 0, 0.8)
    holder:SetBackdropBorderColor(0.5, 0.5, 0.5, 0.8)

    local scroll = CreateFrame("ScrollFrame", nil, holder)
    scroll:SetPoint("TOPLEFT", holder, "TOPLEFT", 6, -6)
    scroll:SetPoint("BOTTOMRIGHT", holder, "BOTTOMRIGHT", -20, 6)

    local content = CreateFrame("Frame", nil, scroll)
    content:SetWidth(width - 26)
    content:SetHeight(height - 12)
    scroll:SetScrollChild(content)

    local text = content:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    text:SetPoint("TOPLEFT", content, "TOPLEFT", 0, 0)
    text:SetWidth(width - 28)
    text:SetJustifyH("LEFT")
    text:SetJustifyV("TOP")

    local bar = slider(holder)
    bar:SetPoint("TOPRIGHT", holder, "TOPRIGHT", -4, -6)
    bar:SetPoint("BOTTOMRIGHT", holder, "BOTTOMRIGHT", -4, 6)

    local function updateScroll()
        local strHeight = text:GetStringHeight() or 0
        local viewHeight = scroll:GetHeight() or (height - 12)
        local contentHeight = math.max(viewHeight, strHeight + 6)
        content:SetHeight(contentHeight)
        local maxScroll = math.max(0, contentHeight - viewHeight)
        bar:SetMinMaxValues(0, maxScroll)
        local cur = math.min(bar:GetValue() or 0, maxScroll)
        bar:SetValue(cur)
        scroll:SetVerticalScroll(cur)
        if maxScroll > 0 then
            bar:Show()
        else
            bar:Hide()
        end
    end

    bar:SetScript("OnValueChanged", function()
        scroll:SetVerticalScroll(this:GetValue() or 0)
    end)

    holder:EnableMouseWheel(true)
    holder:SetScript("OnMouseWheel", function()
        local delta = arg1 or 0
        local _, maxScroll = bar:GetMinMaxValues()
        local cur = bar:GetValue() or 0
        local step = 20
        local newVal = math.max(0, math.min(maxScroll, cur - (delta * step)))
        bar:SetValue(newVal)
        scroll:SetVerticalScroll(newVal)
    end)

    holder.scroll = scroll
    holder.text = text
    holder.bar = bar
    holder.updateScroll = updateScroll

    function holder:SetText(val)
        self.text:SetText(val or "")
        self.bar:SetValue(0)
        self.updateScroll()
    end
    function holder:GetText()
        return self.text:GetText() or ""
    end
    function holder:SetTextColor(r, g, b)
        self.text:SetTextColor(r, g, b)
    end

    return holder
end

function AzerothVoices:BuildEditor(panel)
    local title = createLabel(panel, "Azeroth Voices", 16, -16)
    title:SetFontObject(GameFontNormalLarge)

    local hint = createLabel(panel,
        "Click a bot name to edit traits. Check bots to forget memories.", 16, -38)
    hint:SetFontObject(GameFontHighlightSmall)

    -- Left Column: Roster, Search & Forget
    local left = CreateFrame("Frame", nil, panel)
    left:SetPoint("TOPLEFT", panel, "TOPLEFT", 16, -60)
    left:SetWidth(180)
    left:SetHeight(430)

    createLabel(left, "Search known bots", 0, 0)
    local search = CreateFrame("EditBox", "AzerothVoicesSearchBox", left, "InputBoxTemplate")
    search:SetPoint("TOPLEFT", left, "TOPLEFT", 6, -18)
    search:SetWidth(170)
    search:SetHeight(20)
    search:SetAutoFocus(false)
    search:SetMaxLetters(64)
    search:SetScript("OnEscapePressed", function() this:ClearFocus() end)

    local list = CreateFrame("Frame", nil, left)
    list:SetPoint("TOPLEFT", left, "TOPLEFT", 0, -44)
    list:SetWidth(180)
    list:SetHeight(288) -- 12 rows * 24px
    list:EnableMouseWheel(true)

    local bar = slider(list)
    bar:SetPoint("TOPRIGHT", list, "TOPRIGHT", 0, 0)
    bar:SetPoint("BOTTOMRIGHT", list, "BOTTOMRIGHT", 0, 0)

    local rows = {}
    local empty = createLabel(list, "No known bots", 4, -6)
    empty:SetFontObject(GameFontHighlightSmall)
    empty:SetWidth(150)

    local count = createLabel(left, "", 0, -338)
    count:SetWidth(180)
    count:SetFontObject(GameFontHighlightSmall)

    local all = button(left, "Check all", 88, function()
        if AzerothVoices.forgetQueue then return end
        local filtered = AzerothVoices:GetFilteredRoster(search:GetText())
        local fCount = table.getn(filtered)
        for i = 1, fCount do
            local bot = filtered[i]
            if bot and bot.guid then
                AzerothVoices.checkedBots[bot.guid] = true
            end
        end
        AzerothVoices:UpdateRosterViews()
    end, 20)
    all:SetPoint("TOPLEFT", left, "TOPLEFT", 0, -356)

    local clear = button(left, "Clear", 88, function()
        if AzerothVoices.forgetQueue then return end
        AzerothVoices.checkedBots = {}
        AzerothVoices:UpdateRosterViews()
    end, 20)
    clear:SetPoint("TOPLEFT", all, "TOPRIGHT", 4, 0)

    local forgetBtn = button(left, "Forget selected", 180, function()
        AzerothVoices:ConfirmForget()
    end, 22)
    forgetBtn:SetPoint("TOPLEFT", left, "TOPLEFT", 0, -382)
    panel.forgetBtn = forgetBtn

    -- Right Column: Editor Controls
    local refresh = button(panel, "Refresh", 80, function()
        AzerothVoices:RequestRoster()
    end)
    refresh:SetPoint("TOPRIGHT", panel, "TOPRIGHT", -16, -16)

    local selected = createLabel(panel, "Select a bot", 216, -20)
    selected:SetFontObject(GameFontNormalLarge)

    local contentX = 216
    local contentWidth = 440

    -- Trait 1
    createLabel(panel, "Trait 1", contentX, -50)
    local t1 = createTraitBox(panel, "AzerothVoicesTrait1", contentX + 6, -66, contentWidth - 12)
    panel.trait1 = t1

    -- Trait 2
    createLabel(panel, "Trait 2", contentX, -92)
    local t2 = createTraitBox(panel, "AzerothVoicesTrait2", contentX + 6, -108, contentWidth - 12)
    panel.trait2 = t2

    -- Trait 3
    createLabel(panel, "Trait 3", contentX, -134)
    local t3 = createTraitBox(panel, "AzerothVoicesTrait3", contentX + 6, -150, contentWidth - 12)
    panel.trait3 = t3

    local traitBoxes = { t1, t2, t3 }
    for i = 1, 3 do
        local box = traitBoxes[i]
        box:SetScript("OnEscapePressed", function() this:ClearFocus() end)
        box:SetScript("OnTextChanged", function()
            AzerothVoices:UpdateSaveButton()
        end)
        box:SetScript("OnTabPressed", function()
            this:ClearFocus()
            local nextIdx = math.mod(i, 3) + 1
            traitBoxes[nextIdx]:SetFocus()
        end)
    end

    -- Tone
    createLabel(panel, "Tone (generated)", contentX, -178)
    panel.tone = createToneBox(panel, contentX, -194, contentWidth, 42)

    -- Background Story
    createLabel(panel, "Background Story", contentX, -242)
    panel.backstory = createBackstoryBox(panel, contentX, -258, contentWidth, 148)

    -- Action buttons
    local regenStoryBtn = button(panel, "Regenerate Story", 150, function()
        AzerothVoices:RegenBackstory()
    end, 24)
    regenStoryBtn:SetPoint("TOPLEFT", panel, "TOPLEFT", contentX, -414)
    regenStoryBtn:Disable()
    panel.regenStoryBtn = regenStoryBtn

    local saveBtn = button(panel, "Save Traits", 130, function()
        AzerothVoices:SaveProfile()
    end, 24)
    saveBtn:SetPoint("TOPRIGHT", panel, "TOPRIGHT", -16, -414)
    saveBtn:Disable()
    panel.saveBtn = saveBtn

    -- Status label at bottom
    local status = createLabel(panel, "", 16, -460)
    status:SetPoint("TOPLEFT", panel, "TOPLEFT", 16, -450)
    status:SetPoint("BOTTOMRIGHT", panel, "BOTTOMRIGHT", -16, 8)
    status:SetFontObject(GameFontNormalSmall)
    panel.status = status

    -- List render function
    local updating = false
    panel.refreshList = function()
        if updating then return end
        updating = true
        local filtered = AzerothVoices:GetFilteredRoster(search:GetText())
        local totalFiltered = table.getn(filtered)
        local visible = 12
        local maximum = math.max(0, totalFiltered - visible)
        bar:SetMinMaxValues(0, maximum)
        local offset = math.min(maximum, math.floor(bar:GetValue() or 0))
        bar:SetValue(offset)
        if maximum > 0 then bar:Show() else bar:Hide() end

        for i = 1, visible do
            if not rows[i] then
                local row = CreateFrame("Button", nil, list)
                row:SetPoint("TOPLEFT", list, "TOPLEFT", 0, -(i - 1) * 24)
                row:SetPoint("TOPRIGHT", list, "TOPRIGHT", -16, -(i - 1) * 24)
                row:SetHeight(24)
                row:SetHighlightTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight")

                local check = CreateFrame("CheckButton", nil, row, "UICheckButtonTemplate")
                check:SetWidth(24)
                check:SetHeight(24)
                check:SetPoint("LEFT", row, "LEFT", 0, 0)
                row.check = check

                local text = row:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
                text:SetPoint("LEFT", row, "LEFT", 26, 0)
                text:SetPoint("RIGHT", row, "RIGHT", -2, 0)
                text:SetJustifyH("LEFT")
                row.text = text

                row:SetScript("OnClick", function()
                    if not AzerothVoices.forgetQueue then
                        AzerothVoices:SelectBot(this.guid)
                    end
                end)

                check:SetScript("OnClick", function()
                    if AzerothVoices.forgetQueue then return end
                    local parentRow = this:GetParent()
                    if parentRow and parentRow.guid then
                        AzerothVoices.checkedBots[parentRow.guid] = this:GetChecked() and true or nil
                        AzerothVoices:UpdateRosterViews()
                    end
                end)

                row:SetScript("OnEnter", function()
                    GameTooltip:SetOwner(this, "ANCHOR_RIGHT")
                    GameTooltip:SetText(this.botName or "")
                    GameTooltip:Show()
                end)
                row:SetScript("OnLeave", function()
                    GameTooltip:Hide()
                end)

                rows[i] = row
            end

            local row = rows[i]
            local bot = filtered[offset + i]
            if bot then
                row.guid = bot.guid
                row.botName = bot.name
                row.text:SetText(bot.name)
                row.check:SetChecked(AzerothVoices.checkedBots[bot.guid] or false)
                if AzerothVoices.forgetQueue then
                    row.check:Disable()
                else
                    row.check:Enable()
                end
                if bot.guid == AzerothVoices.selectedGuid then
                    row:LockHighlight()
                    row.text:SetTextColor(1, 0.82, 0)
                else
                    row:UnlockHighlight()
                    row.text:SetTextColor(1, 1, 1)
                end
                row:Show()
            else
                row:Hide()
            end
        end

        local totalRoster = table.getn(AzerothVoices.roster or {})
        local checkedCount = 0
        for _ in pairs(AzerothVoices.checkedBots) do
            checkedCount = checkedCount + 1
        end

        count:SetText(totalFiltered .. "/" .. totalRoster .. " bots | " .. checkedCount .. " checked")
        selected:SetText(AzerothVoices:GetSelectedName() or "Select a bot")

        if totalFiltered == 0 then
            empty:SetText(totalRoster == 0 and "No known bots" or "No matches")
            empty:Show()
        else
            empty:Hide()
        end

        if checkedCount > 0 and not AzerothVoices.forgetQueue and not AzerothVoices.pendingRoster then
            panel.forgetBtn:Enable()
        else
            panel.forgetBtn:Disable()
        end

        if AzerothVoices.forgetQueue then
            all:Disable()
            clear:Disable()
            refresh:Disable()
        else
            all:Enable()
            clear:Enable()
            refresh:Enable()
        end

        updating = false
    end

    bar:SetScript("OnValueChanged", panel.refreshList)
    list:SetScript("OnMouseWheel", function()
        local delta = arg1 or 0
        local _, maximum = bar:GetMinMaxValues()
        local cur = bar:GetValue() or 0
        bar:SetValue(math.max(0, math.min(maximum, cur - (delta * 3))))
    end)
    search:SetScript("OnTextChanged", function()
        bar:SetValue(0)
        panel.refreshList()
    end)

    panel:SetScript("OnShow", function()
        panel.refreshList()
    end)

    panel.refreshList()
end

function AzerothVoices:BuildFrame()
    if self.frame then return end

    local frame = CreateFrame("Frame", "AzerothVoicesMainFrame", UIParent)
    frame:SetWidth(680)
    frame:SetHeight(480)
    frame:SetClampedToScreen(true)
    frame:SetMovable(true)
    frame:EnableMouse(true)
    frame:RegisterForDrag("LeftButton")
    frame:SetScript("OnDragStart", function() this:StartMoving() end)
    frame:SetScript("OnDragStop", function()
        this:StopMovingOrSizing()
        AzerothVoices:SaveWindowPosition()
    end)
    frame:SetBackdrop({
        bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
        edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
        tile = true, tileSize = 16, edgeSize = 16,
        insets = { left = 4, right = 4, top = 4, bottom = 4 },
    })
    frame:Hide()

    self.frame = frame
    self:BuildEditor(frame)

    local close = CreateFrame("Button", nil, frame, "UIPanelCloseButton")
    close:SetPoint("TOPRIGHT", frame, "TOPRIGHT", -4, -4)

    table.insert(UISpecialFrames, "AzerothVoicesMainFrame")
    self:RestoreWindowPosition()
end
