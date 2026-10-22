-- Single-window responsive editor for Turtle WoW 1.18.1 / Vanilla Interface 11200.
-- Follows the exact 680x640 dimensions and responsive UI framework of Chatter.
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

local function createEditBox(parent, name, x, y, width, height)
    local holder = CreateFrame("Frame", nil, parent)
    holder:SetPoint("TOPLEFT", parent, "TOPLEFT", x, y)
    holder:SetWidth(width)
    holder:SetHeight(height or 24)
    holder:SetBackdrop({
        bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
        edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
        tile = true,
        tileSize = 8,
        edgeSize = 12,
        insets = { left = 3, right = 3, top = 3, bottom = 3 }
    })
    holder:SetBackdropColor(0, 0, 0, 0.85)
    holder:SetBackdropBorderColor(0.55, 0.55, 0.55, 1)

    local box = CreateFrame("EditBox", name, holder)
    box:SetAutoFocus(false)
    box:SetMaxLetters(64)
    box:SetFontObject(GameFontHighlight)
    box:SetPoint("TOPLEFT", holder, "TOPLEFT", 6, -2)
    box:SetPoint("BOTTOMRIGHT", holder, "BOTTOMRIGHT", -6, 2)
    box:SetJustifyH("LEFT")

    box.holder = holder
    holder.editBox = box
    return box
end

local function getTextHeight(fs, wrapWidth)
    if not fs then return 0 end
    local h = 0
    if fs.GetStringHeight then
        local ok, val = pcall(fs.GetStringHeight, fs)
        if ok and val and val > 0 then
            h = val
        end
    end
    if (not h or h <= 0) and fs.GetHeight then
        local ok, val = pcall(fs.GetHeight, fs)
        if ok and val and val > 0 then
            h = val
        end
    end
    if h and h > 0 then
        return h
    end

    local raw = ""
    if fs.GetText then
        local ok, val = pcall(fs.GetText, fs)
        if ok and val then raw = val end
    end
    local len = string.len(raw)
    if len > 0 then
        local w = wrapWidth or 420
        local charsPerLine = math.max(20, math.floor(w / 7))
        local totalLines = 0
        local startIdx = 1
        while true do
            local nl = string.find(raw, "\n", startIdx, true)
            local linePart
            if nl then
                linePart = string.sub(raw, startIdx, nl - 1)
                startIdx = nl + 1
            else
                linePart = string.sub(raw, startIdx)
            end
            local partLen = string.len(linePart)
            local subLines = math.max(1, math.floor(partLen / charsPerLine) + (math.mod(partLen, charsPerLine) > 0 and 1 or 0))
            totalLines = totalLines + subLines
            if not nl then break end
        end
        return totalLines * 15
    end
    return 0
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
        tileSize = 8,
        edgeSize = 12,
        insets = { left = 3, right = 3, top = 3, bottom = 3 }
    })
    holder:SetBackdropColor(0, 0, 0, 0.85)
    holder:SetBackdropBorderColor(0.55, 0.55, 0.55, 1)

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
        local strHeight = getTextHeight(text, width - 28)
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

    local function onMouseWheel()
        local delta = arg1 or 0
        local _, maxScroll = bar:GetMinMaxValues()
        local cur = bar:GetValue() or 0
        local step = 16
        local newVal = math.max(0, math.min(maxScroll, cur - (delta * step)))
        bar:SetValue(newVal)
        scroll:SetVerticalScroll(newVal)
    end

    holder:EnableMouseWheel(true)
    holder:SetScript("OnMouseWheel", onMouseWheel)
    scroll:EnableMouseWheel(true)
    scroll:SetScript("OnMouseWheel", onMouseWheel)
    content:EnableMouseWheel(true)
    content:SetScript("OnMouseWheel", onMouseWheel)

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

local function createBackstoryBox(parent, x, y, width, height)
    local holder = CreateFrame("Frame", nil, parent)
    holder:SetPoint("TOPLEFT", parent, "TOPLEFT", x, y)
    holder:SetWidth(width)
    holder:SetHeight(height)
    holder:SetBackdrop({
        bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
        edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
        tile = true,
        tileSize = 8,
        edgeSize = 12,
        insets = { left = 3, right = 3, top = 3, bottom = 3 }
    })
    holder:SetBackdropColor(0, 0, 0, 0.85)
    holder:SetBackdropBorderColor(0.55, 0.55, 0.55, 1)

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
        local strHeight = getTextHeight(text, width - 28)
        local viewHeight = scroll:GetHeight() or (height - 12)
        local contentHeight = math.max(viewHeight, strHeight + 16)
        content:SetHeight(contentHeight)
        text:SetHeight(contentHeight)
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

    local function onMouseWheel()
        local delta = arg1 or 0
        local _, maxScroll = bar:GetMinMaxValues()
        local cur = bar:GetValue() or 0
        local step = 20
        local newVal = math.max(0, math.min(maxScroll, cur - (delta * step)))
        bar:SetValue(newVal)
        scroll:SetVerticalScroll(newVal)
    end

    holder:EnableMouseWheel(true)
    holder:SetScript("OnMouseWheel", onMouseWheel)
    scroll:EnableMouseWheel(true)
    scroll:SetScript("OnMouseWheel", onMouseWheel)
    content:EnableMouseWheel(true)
    content:SetScript("OnMouseWheel", onMouseWheel)

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
    panel.currentMode = panel.currentMode or "full"

    local title = createLabel(panel, "Chatter / Azeroth Voices", 16, -16)
    title:SetFontObject(GameFontNormalLarge)

    local hint = createLabel(panel,
        "Click a bot name to edit traits. Check bots to forget memories.", 16, -38)
    hint:SetFontObject(GameFontHighlightSmall)

    -- Left Column: Roster, Search & Forget (Width 144, Height 530)
    local left = CreateFrame("Frame", nil, panel)
    left:SetPoint("TOPLEFT", panel, "TOPLEFT", 16, -76)
    left:SetPoint("BOTTOMLEFT", panel, "BOTTOMLEFT", 16, 72)
    left:SetWidth(144)

    createLabel(left, "Search known bots", 0, 0)
    local search = createEditBox(left, "AzerothVoicesSearchBox", 0, -18, 144, 24)
    search:SetScript("OnEscapePressed", function() this:ClearFocus() end)

    local list = CreateFrame("Frame", nil, left)
    list:SetPoint("TOPLEFT", left, "TOPLEFT", 0, -48)
    list:SetPoint("TOPRIGHT", left, "TOPRIGHT", 0, -48)
    list:SetHeight(384)
    list:EnableMouseWheel(true)

    local bar = slider(list)
    bar:SetPoint("TOPRIGHT", list, "TOPRIGHT", 0, 0)
    bar:SetPoint("BOTTOMRIGHT", list, "BOTTOMRIGHT", 0, 0)

    local rows = {}
    local empty = createLabel(list, "No known bots", 2, -6)
    empty:SetFontObject(GameFontHighlightSmall)
    empty:SetWidth(124)

    local count = createLabel(left, "", 0, 34)
    count:ClearAllPoints()
    count:SetPoint("BOTTOMLEFT", left, "BOTTOMLEFT", 0, 34)
    count:SetWidth(144)
    count:SetFontObject(GameFontHighlightSmall)

    local all = button(left, "Check all", 82, function()
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
    end, 22)
    all:SetPoint("BOTTOMLEFT", left, "BOTTOMLEFT", 0, 6)

    local clear = button(left, "Clear", 58, function()
        if AzerothVoices.forgetQueue then return end
        AzerothVoices.checkedBots = {}
        AzerothVoices:UpdateRosterViews()
    end, 22)
    clear:SetPoint("LEFT", all, "RIGHT", 4, 0)

    local forgetBtn = button(panel, "Forget selected", 144, function()
        AzerothVoices:ConfirmForget()
    end, 24)
    forgetBtn:SetPoint("BOTTOMLEFT", panel, "BOTTOMLEFT", 16, 44)
    panel.forgetBtn = forgetBtn

    -- Right Column Header Controls
    local refresh = button(panel, "Refresh", 80, function()
        AzerothVoices:RequestRoster()
    end, 24)
    refresh:SetPoint("TOPRIGHT", panel, "TOPRIGHT", -16, -72)

    local selected = createLabel(panel, "Select a bot", 172, -76)
    selected:SetPoint("TOPRIGHT", refresh, "TOPLEFT", -8, -4)
    selected:SetHeight(16)
    selected:SetFontObject(GameFontNormalLarge)

    -- Right Column Scrollable Content Frame (exact 172, -104 to -34, 76)
    local scroll = CreateFrame("ScrollFrame", nil, panel)
    scroll:SetPoint("TOPLEFT", panel, "TOPLEFT", 172, -104)
    scroll:SetPoint("BOTTOMRIGHT", panel, "BOTTOMRIGHT", -34, 76)

    local content = CreateFrame("Frame", nil, scroll)
    content:SetWidth(470)
    content:SetHeight(454)
    scroll:SetScrollChild(content)

    local editorBar = slider(panel)
    editorBar:SetPoint("TOPLEFT", scroll, "TOPRIGHT", 4, 0)
    editorBar:SetPoint("BOTTOMLEFT", scroll, "BOTTOMRIGHT", 4, 0)
    editorBar:SetScript("OnValueChanged", function()
        scroll:SetVerticalScroll(this:GetValue() or 0)
    end)

    scroll:EnableMouseWheel(true)
    scroll:SetScript("OnMouseWheel", function()
        local delta = arg1 or 0
        local _, maximum = editorBar:GetMinMaxValues()
        local cur = editorBar:GetValue() or 0
        editorBar:SetValue(math.max(0, math.min(maximum, cur - delta * 28)))
    end)

    local function updateEditorScroll()
        local viewHeight = scroll:GetHeight() or 460
        local cHeight = content:GetHeight() or 454
        local maximum = math.max(0, cHeight - viewHeight)
        editorBar:SetMinMaxValues(0, maximum)
        local cur = math.min(editorBar:GetValue() or 0, maximum)
        editorBar:SetValue(cur)
        scroll:SetVerticalScroll(cur)
        if maximum > 0 then
            editorBar:Show()
        else
            editorBar:Hide()
        end
    end

    local contentWidth = 460

    -- Trait 1
    local l1 = createLabel(content, "Trait 1", 0, 0)
    local regenTraitsBtn = button(content, "Generate Traits", 124, function()
        AzerothVoices:RegenTraits()
    end, 20)
    regenTraitsBtn:SetPoint("TOPRIGHT", content, "TOPLEFT", contentWidth, 2)
    regenTraitsBtn:Disable()
    panel.regenTraitsBtn = regenTraitsBtn

    local t1 = createEditBox(content, "AzerothVoicesTrait1", 0, -18, contentWidth, 24)
    panel.trait1 = t1

    -- Trait 2
    local l2 = createLabel(content, "Trait 2", 0, -48)
    local t2 = createEditBox(content, "AzerothVoicesTrait2", 0, -66, contentWidth, 24)
    panel.trait2 = t2

    -- Trait 3
    local l3 = createLabel(content, "Trait 3", 0, -96)
    local t3 = createEditBox(content, "AzerothVoicesTrait3", 0, -114, contentWidth, 24)
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
    local lTone = createLabel(content, "Tone (generated)", 0, -148)
    local regenToneBtn = button(content, "Generate Tone", 114, function()
        AzerothVoices:RegenTone()
    end, 20)
    regenToneBtn:SetPoint("TOPRIGHT", content, "TOPLEFT", contentWidth, -146)
    regenToneBtn:Disable()
    panel.regenToneBtn = regenToneBtn

    panel.tone = createToneBox(content, 0, -166, contentWidth, 58)

    -- Background Story
    local lStory = createLabel(content, "Background Story", 0, -236)
    panel.backstory = createBackstoryBox(content, 0, -254, contentWidth, 200)

    -- Regenerate Story Button
    local regenStoryBtn = button(content, "Generate Story", 120, function()
        AzerothVoices:RegenBackstory()
    end, 24)
    regenStoryBtn:SetPoint("TOPLEFT", content, "TOPLEFT", 0, -466)
    regenStoryBtn:SetScript("OnEnter", function()
        GameTooltip:SetOwner(this, "ANCHOR_TOP")
        GameTooltip:ClearLines()
        GameTooltip:AddLine("Generate Background Story", 1, 1, 1)
        GameTooltip:AddLine("Generates an authentic character origin backstory via LLM.", 0.8, 0.8, 0.8, 1)
        GameTooltip:AddLine("Injected when the bot is directly asked about their past.", 0.3, 1, 0.3, 1)
        GameTooltip:Show()
    end)
    regenStoryBtn:SetScript("OnLeave", function()
        GameTooltip:Hide()
    end)
    regenStoryBtn:Disable()
    panel.regenStoryBtn = regenStoryBtn

    -- Generate All Info Button
    local regenAllBtn = button(content, "Generate All Info", 140, function()
        AzerothVoices:RegenAll()
    end, 24)
    regenAllBtn:SetPoint("LEFT", regenStoryBtn, "RIGHT", 8, 0)
    regenAllBtn:Disable()
    panel.regenAllBtn = regenAllBtn

    -- Sentiment Section Header & Reset Button
    local lSentiment = createLabel(content, "Sentiment Indicator", 0, -504)
    panel.sentimentLabel = lSentiment

    local resetSentimentBtn = button(content, "Reset Sentiment", 130, function()
        AzerothVoices:ResetSentiment()
    end, 22)
    resetSentimentBtn:SetPoint("TOPRIGHT", content, "TOPLEFT", contentWidth, -500)
    resetSentimentBtn:Disable()
    panel.resetSentimentBtn = resetSentimentBtn

    -- Sentiment Status Bar (low -100 to high +100)
    local barHolder = CreateFrame("Frame", nil, content)
    barHolder:SetPoint("TOPLEFT", content, "TOPLEFT", 0, -528)
    barHolder:SetWidth(contentWidth)
    barHolder:SetHeight(18)
    barHolder:SetBackdrop({
        bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
        edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
        tile = true,
        tileSize = 8,
        edgeSize = 10,
        insets = { left = 2, right = 2, top = 2, bottom = 2 }
    })
    barHolder:SetBackdropColor(0, 0, 0, 0.85)
    barHolder:SetBackdropBorderColor(0.5, 0.5, 0.5, 1)

    local bar = CreateFrame("StatusBar", nil, barHolder)
    bar:SetPoint("TOPLEFT", barHolder, "TOPLEFT", 3, -3)
    bar:SetPoint("BOTTOMRIGHT", barHolder, "BOTTOMRIGHT", -3, 3)
    bar:SetStatusBarTexture("Interface\\TargetingFrame\\UI-StatusBar")
    if bar:GetStatusBarTexture() and bar:GetStatusBarTexture().SetHorizTile then
        bar:GetStatusBarTexture():SetHorizTile(false)
    end
    bar:SetMinMaxValues(0, 200)
    bar:SetValue(100)
    bar:SetStatusBarColor(0.9, 0.82, 0.2)

    local barText = bar:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    barText:SetPoint("CENTER", bar, "CENTER", 0, 0)
    barText:SetText("Score: 0 | Neutral")
    bar.text = barText
    panel.sentimentBar = bar
    panel.sentimentBarHolder = barHolder

    barHolder:EnableMouse(true)
    barHolder:SetScript("OnEnter", function()
        GameTooltip:SetOwner(this, "ANCHOR_RIGHT")
        GameTooltip:ClearLines()
        local score = AzerothVoices.loadedSentiment or 0
        local tier, r, g, b = AzerothVoices:GetSentimentTier(score)
        local sign = ""
        if score > 0 then sign = "+" end
        GameTooltip:AddLine("Bot Sentiment toward You", 1, 0.82, 0)
        GameTooltip:AddDoubleLine("Current Score:", sign .. score .. " (" .. tier .. ")", 1, 1, 1, r, g, b)
        GameTooltip:AddLine(" ")
        GameTooltip:AddLine("Sentiment Scale:", 0.8, 0.8, 0.8)
        GameTooltip:AddDoubleLine("  Hostile:", "-100 to -40", 0.85, 0.2, 0.2, 0.85, 0.2, 0.2)
        GameTooltip:AddDoubleLine("  Cold:", "-39 to -10", 0.9, 0.55, 0.2, 0.9, 0.55, 0.2)
        GameTooltip:AddDoubleLine("  Neutral:", "-9 to +9", 0.9, 0.82, 0.2, 0.9, 0.82, 0.2)
        GameTooltip:AddDoubleLine("  Warm:", "+10 to +39", 0.4, 0.85, 0.3, 0.4, 0.85, 0.3)
        GameTooltip:AddDoubleLine("  Trusted:", "+40 to +100", 0.2, 0.85, 0.5, 0.2, 0.85, 0.5)
        GameTooltip:AddLine(" ")
        GameTooltip:AddLine("Evolves dynamically through party chat, questing, and dialogue.", 0.6, 0.6, 0.6, 1)
        GameTooltip:Show()
    end)
    barHolder:SetScript("OnLeave", function()
        GameTooltip:Hide()
    end)

    -- Scale Labels below the bar
    local scaleLow = content:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
    scaleLow:SetPoint("TOPLEFT", barHolder, "BOTTOMLEFT", 2, -3)
    scaleLow:SetText("-100 (Hostile)")
    scaleLow:SetTextColor(0.85, 0.3, 0.3)
    panel.sentimentScaleLow = scaleLow

    local scaleMid = content:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
    scaleMid:SetPoint("TOP", barHolder, "BOTTOM", 0, -3)
    scaleMid:SetText("0 (Neutral)")
    scaleMid:SetTextColor(0.9, 0.82, 0.2)
    panel.sentimentScaleMid = scaleMid

    local scaleHigh = content:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
    scaleHigh:SetPoint("TOPRIGHT", barHolder, "BOTTOMRIGHT", -2, -3)
    scaleHigh:SetText("+100 (Trusted)")
    scaleHigh:SetTextColor(0.3, 0.85, 0.4)
    panel.sentimentScaleHigh = scaleHigh

    content:SetHeight(585)

    -- Status label at bottom
    local status = createLabel(panel, "", 16, 10)
    status:ClearAllPoints()
    status:SetPoint("BOTTOMLEFT", panel, "BOTTOMLEFT", 16, 10)
    status:SetPoint("BOTTOMRIGHT", panel, "BOTTOMRIGHT", -16, 10)
    status:SetFontObject(GameFontNormalSmall)
    status:SetHeight(28)
    panel.status = status

    -- Top Mode Tabs (Full Overview, Bot Traits, Background Stories)
    local function applyMode(mode)
        panel.currentMode = mode
        if mode == "traits" then
            l1:Show(); t1.holder:Show()
            l2:Show(); t2.holder:Show()
            l3:Show(); t3.holder:Show()
            regenTraitsBtn:Show()
            lTone:Show(); panel.tone:Show()
            regenToneBtn:Show()
            lStory:Hide(); panel.backstory:Hide()
            regenStoryBtn:Hide()
            regenAllBtn:Hide()
            lSentiment:Hide(); barHolder:Hide(); resetSentimentBtn:Hide()
            scaleLow:Hide(); scaleMid:Hide(); scaleHigh:Hide()
            forgetBtn:Show()
            all:Show(); clear:Show()
            content:SetHeight(230)
        elseif mode == "stories" then
            l1:Hide(); t1.holder:Hide()
            l2:Hide(); t2.holder:Hide()
            l3:Hide(); t3.holder:Hide()
            regenTraitsBtn:Hide()
            lTone:Hide(); panel.tone:Hide()
            regenToneBtn:Hide()
            lStory:Show()
            panel.backstory:Show()
            regenStoryBtn:Show()
            regenAllBtn:Hide()
            lSentiment:Hide(); barHolder:Hide(); resetSentimentBtn:Hide()
            scaleLow:Hide(); scaleMid:Hide(); scaleHigh:Hide()
            forgetBtn:Hide()
            all:Hide(); clear:Hide()
            content:SetHeight(440)
        else
            -- "full" mode
            l1:Show(); t1.holder:Show()
            l2:Show(); t2.holder:Show()
            l3:Show(); t3.holder:Show()
            regenTraitsBtn:Show()
            lTone:Show(); panel.tone:Show()
            regenToneBtn:Show()
            lStory:Show(); panel.backstory:Show()
            regenStoryBtn:Show()
            regenAllBtn:Show()
            lSentiment:Show(); barHolder:Show(); resetSentimentBtn:Show()
            scaleLow:Show(); scaleMid:Show(); scaleHigh:Show()
            forgetBtn:Show()
            all:Show(); clear:Show()
            content:SetHeight(585)
        end
        updateEditorScroll()
        AzerothVoices:UpdateSaveButton()
    end

    panel.applyMode = applyMode
    applyMode("full")

    -- List render function
    local updating = false
    panel.refreshList = function()
        if updating then return end
        updating = true
        local filtered = AzerothVoices:GetFilteredRoster(search:GetText())
        local totalFiltered = table.getn(filtered)
        local visible = 16
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
                row:EnableMouseWheel(true)
                row:SetScript("OnMouseWheel", function()
                    local delta = arg1 or 0
                    local _, maxVal = bar:GetMinMaxValues()
                    local cur = bar:GetValue() or 0
                    bar:SetValue(math.max(0, math.min(maxVal, cur - (delta * 3))))
                end)

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

        local totalRows = table.getn(rows)
        for i = visible + 1, totalRows do
            if rows[i] then
                rows[i]:Hide()
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
        updateEditorScroll()
        panel.refreshList()
    end)

    updateEditorScroll()
    panel.refreshList()
end

function AzerothVoices:BuildFrame()
    if self.frame then return end

    local frame = CreateFrame("Frame", "AzerothVoicesMainFrame", UIParent)
    frame:SetWidth(680)
    frame:SetHeight(640)
    local screenWidth = UIParent:GetWidth() or 1024
    local screenHeight = UIParent:GetHeight() or 768
    local scale = math.min(1, math.min((screenWidth - 32) / 680, (screenHeight - 32) / 640))
    if scale > 0 and scale < 1 then
        frame:SetScale(scale)
    end
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

local FAMILIAR_ICONS = {
    { icon = "Interface\\Icons\\Spell_Holy_Silence", name = "Speaking Voice" },
    { icon = "Interface\\Icons\\INV_Misc_Horn_02", name = "Battle Horn" },
    { icon = "Interface\\Icons\\Spell_Shadow_MindSteal", name = "AI Mind" },
    { icon = "Interface\\Icons\\INV_Letter_15", name = "Chat Letter" },
    { icon = "Interface\\Icons\\INV_Misc_Book_09", name = "Story Tome" },
    { icon = "Interface\\Icons\\INV_Misc_Gear_01", name = "Clockwork Cog" },
    { icon = "Interface\\Icons\\INV_Misc_Head_Dragon_01", name = "Dragon Head" },
}

local MINIMAP_BUTTON_RADIUS = 80
local MINIMAP_CENTER_OFFSET = 70
local MINIMAP_DEFAULT_ANGLE = -35

function AzerothVoices:GetMinimapIconInfo()
    local idx = (AzerothVoicesDB and AzerothVoicesDB.minimapIconIndex) or 1
    local total = table.getn(FAMILIAR_ICONS)
    if idx < 1 or idx > total then
        idx = 1
    end
    return FAMILIAR_ICONS[idx].icon, FAMILIAR_ICONS[idx].name, idx, total
end

function AzerothVoices:CycleMinimapIcon()
    AzerothVoicesDB = AzerothVoicesDB or {}
    local total = table.getn(FAMILIAR_ICONS)
    local cur = AzerothVoicesDB.minimapIconIndex or 1
    local nextIdx = math.mod(cur, total) + 1
    AzerothVoicesDB.minimapIconIndex = nextIdx
    AzerothVoicesDB.minimapIcon = FAMILIAR_ICONS[nextIdx].icon
    if self.minimapButton and self.minimapButton.icon then
        self.minimapButton.icon:SetTexture(FAMILIAR_ICONS[nextIdx].icon)
    end
    if DEFAULT_CHAT_FRAME and DEFAULT_CHAT_FRAME.AddMessage then
        DEFAULT_CHAT_FRAME:AddMessage("|cffffd100[AzerothVoices]|r Minimap icon changed to: |cff00ff00" .. FAMILIAR_ICONS[nextIdx].name .. "|r")
    end
end

function AzerothVoices:SetMinimapButtonPosition(angle)
    if not self.minimapButton then return end
    local parentMinimap = Minimap or UIParent
    if not parentMinimap then return end
    angle = angle or (AzerothVoicesDB and AzerothVoicesDB.minimapAngle) or MINIMAP_DEFAULT_ANGLE
    local radians = math.rad(angle)
    local x = 52 - MINIMAP_BUTTON_RADIUS * math.cos(radians)
    local y = MINIMAP_BUTTON_RADIUS * math.sin(radians) - 52
    self.minimapButton:ClearAllPoints()
    self.minimapButton:SetPoint("TOPLEFT", parentMinimap, "TOPLEFT", x, y)
end

function AzerothVoices:UpdateMinimapButtonPosition()
    if not self.minimapButton then return end
    local parentMinimap = Minimap or UIParent
    if not parentMinimap then return end
    local cursorX, cursorY = GetCursorPosition()
    local scale = (UIParent and UIParent:GetScale()) or 1
    local minimapLeft = (parentMinimap.GetLeft and parentMinimap:GetLeft()) or 0
    local minimapBottom = (parentMinimap.GetBottom and parentMinimap:GetBottom()) or 0
    local x = minimapLeft - cursorX / scale + MINIMAP_CENTER_OFFSET
    local y = cursorY / scale - minimapBottom - MINIMAP_CENTER_OFFSET
    if x == 0 and y == 0 then return end
    local angle = math.deg(math.atan2(y, x))
    AzerothVoicesDB = AzerothVoicesDB or {}
    AzerothVoicesDB.minimapAngle = angle
    self:SetMinimapButtonPosition(angle)
end

function AzerothVoices:OpenMinimapMenu(anchor)
    if self.minimapMenu and self.minimapMenu:IsShown() then
        self.minimapMenu:Hide()
        return
    end

    if not self.minimapMenu then
        local menu = CreateFrame("Frame", "AzerothVoicesMinimapMenu", UIParent)
        menu:SetWidth(184)
        menu:SetHeight(214)
        menu:SetFrameStrata("DIALOG")
        menu:SetFrameLevel(20)
        menu:SetClampedToScreen(true)
        menu:SetBackdrop({
            bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
            edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
            tile = true, tileSize = 16, edgeSize = 16,
            insets = { left = 4, right = 4, top = 4, bottom = 4 },
        })
        menu:SetBackdropColor(0.08, 0.08, 0.1, 0.95)
        menu:SetBackdropBorderColor(0.8, 0.7, 0.2, 1)

        local title = menu:CreateFontString(nil, "ARTWORK", "GameFontHighlightSmall")
        title:SetPoint("TOP", menu, "TOP", 0, -8)
        title:SetText("Azeroth Voices Quick Actions")
        menu.title = title

        local botLabel = menu:CreateFontString(nil, "ARTWORK", "GameFontNormalSmall")
        botLabel:SetPoint("TOP", title, "BOTTOM", 0, -4)
        botLabel:SetWidth(164)
        botLabel:SetJustifyH("CENTER")
        menu.botLabel = botLabel

        local function makeItem(text, yOffset, onClick)
            local btn = CreateFrame("Button", nil, menu)
            btn:SetWidth(168)
            btn:SetHeight(20)
            btn:SetPoint("TOPLEFT", menu, "TOPLEFT", 8, yOffset)
            btn:SetHighlightTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight")

            local fs = btn:CreateFontString(nil, "ARTWORK", "GameFontNormalSmall")
            fs:SetPoint("LEFT", btn, "LEFT", 4, 0)
            fs:SetText(text)
            btn.label = fs

            btn:SetScript("OnClick", function()
                menu:Hide()
                onClick()
            end)
            return btn
        end

        makeItem("|cff00ff00Generate All Info|r", -36, function()
            AzerothVoices:RegenAll()
        end)
        makeItem("Generate Traits", -58, function()
            AzerothVoices:RegenTraits()
        end)
        makeItem("Generate Tone", -80, function()
            AzerothVoices:RegenTone()
        end)
        makeItem("Generate Story", -102, function()
            AzerothVoices:RegenBackstory()
        end)
        makeItem("Open Editor Window", -126, function()
            AzerothVoices:Toggle()
        end)
        makeItem("Cycle Minimap Icon", -148, function()
            AzerothVoices:CycleMinimapIcon()
        end)
        makeItem("|cffff2020Close Menu|r", -174, function()
            menu:Hide()
        end)

        self.minimapMenu = menu
    end

    local bot = self:GetSelectedBotName()
    if bot then
        self.minimapMenu.botLabel:SetText("|cff00ff00Bot: " .. bot .. "|r")
    else
        self.minimapMenu.botLabel:SetText("|cffaaaaaaNo bot selected|r")
    end

    self.minimapMenu:ClearAllPoints()
    if anchor then
        self.minimapMenu:SetPoint("TOPRIGHT", anchor, "BOTTOMLEFT", -2, 4)
    else
        self.minimapMenu:SetPoint("CENTER", UIParent, "CENTER", 0, 0)
    end
    self.minimapMenu:Show()
    self.minimapMenu:Raise()
end

function AzerothVoices:UpdateMinimapTooltip(button)
    if not button or not GameTooltip then return end
    local iconTex, iconName, iconIdx, iconTotal = self:GetMinimapIconInfo()
    GameTooltip:SetOwner(button, "ANCHOR_LEFT")
    if GameTooltip.ClearLines then
        GameTooltip:ClearLines()
    end
    GameTooltip:AddLine("Azeroth Voices - Bot LLM Dialogue", 1.0, 0.82, 0.0)
    GameTooltip:AddLine("Icon: |cffffffff" .. iconName .. "|r (" .. iconIdx .. "/" .. iconTotal .. ")", 0.7, 0.7, 0.7)

    local botName = self:GetSelectedBotName()
    if botName then
        GameTooltip:AddLine("Selected Bot: |cff00ff00" .. botName .. "|r", 0.8, 0.8, 0.8)
    else
        GameTooltip:AddLine("Selected Bot: |cffaaaaaaNone (Select from roster)|r", 0.8, 0.8, 0.8)
    end

    if self.serverStatus then
        local st = self.serverStatus
        local engineStr = st.enabled and "|cff00ff00Active|r" or "|cffff2020Disabled|r"
        local apiStr = st.apiConfigured and "|cff00ff00Ready|r" or "|cffffa500Unconfigured|r"
        GameTooltip:AddLine("LLM Engine: " .. engineStr .. " |cff888888(|rAPI: " .. apiStr .. "|cff888888)|r", 0.8, 0.8, 0.8)
        if st.model and st.model ~= "" and st.model ~= "-" then
            GameTooltip:AddLine("Model: |cffffffff" .. st.model .. "|r", 0.7, 0.7, 0.7)
        end
        if st.endpoint and st.endpoint ~= "" and st.endpoint ~= "-" then
            GameTooltip:AddLine("Endpoint: |cffaaaaaa" .. st.endpoint .. "|r", 0.6, 0.6, 0.6)
        end
    else
        GameTooltip:AddLine("LLM Engine: |cff00ff00Connecting...|r", 0.8, 0.8, 0.8)
        self:RequestStatus()
    end
    GameTooltip:AddLine("SQL Database: |cff00ff00Connected (Auto-Commit)|r", 0.8, 0.8, 0.8)

    GameTooltip:AddLine(" ")
    GameTooltip:AddLine("|cffffd100Direct Actions (Click Icon):|r", 1.0, 0.82, 0.0)
    GameTooltip:AddLine("  |cff00ff00Shift + Left-Click:|r Generate All Info (Traits+Tone+Story)", 0.9, 0.9, 0.9)
    GameTooltip:AddLine("  |cff00ff00Alt + Left-Click:|r Generate Traits (Lore pool & SQL override)", 0.9, 0.9, 0.9)
    GameTooltip:AddLine("  |cff00ff00Ctrl + Left-Click:|r Generate Tone (via LLM API)", 0.9, 0.9, 0.9)
    GameTooltip:AddLine("  |cff00ff00Shift + Right-Click:|r Generate Background Story (via LLM)", 0.9, 0.9, 0.9)
    GameTooltip:AddLine(" ")
    if self.frame and self.frame:IsShown() then
        GameTooltip:AddLine("|cff00ff00Left-Click:|r Hide Editor Window", 0.8, 0.8, 0.8)
    else
        GameTooltip:AddLine("|cff00ff00Left-Click:|r Open Editor Window", 0.8, 0.8, 0.8)
    end
    GameTooltip:AddLine("|cff00ff00Alt + Right-Click:|r Quick Actions Menu", 0.8, 0.8, 0.8)
    GameTooltip:AddLine("|cff00ff00Right-Click:|r Cycle Familiar Icon", 0.8, 0.8, 0.8)
    GameTooltip:AddLine("|cff00ff00Drag:|r Move around Minimap", 0.8, 0.8, 0.8)
    GameTooltip:AddLine("|cffffd100Commands:|r |cffffffff/azerothvoices|r or |cffffffff/av|r", 0.6, 0.6, 0.6)
    GameTooltip:Show()
end

function AzerothVoices:ToggleMinimapButton()
    AzerothVoicesDB = AzerothVoicesDB or {}
    if not self.minimapButton then
        self:CreateMinimapButton()
    end
    if not self.minimapButton then return end
    if self.minimapButton:IsShown() then
        self.minimapButton:Hide()
        AzerothVoicesDB.hideMinimap = true
        if DEFAULT_CHAT_FRAME and DEFAULT_CHAT_FRAME.AddMessage then
            DEFAULT_CHAT_FRAME:AddMessage("|cffffd100[AzerothVoices]|r Minimap button hidden. Type |cff00ff00/av minimap|r to restore.")
        end
    else
        self.minimapButton:Show()
        AzerothVoicesDB.hideMinimap = false
        if DEFAULT_CHAT_FRAME and DEFAULT_CHAT_FRAME.AddMessage then
            DEFAULT_CHAT_FRAME:AddMessage("|cffffd100[AzerothVoices]|r Minimap button shown.")
        end
    end
end

function AzerothVoices:ResetMinimapPosition()
    AzerothVoicesDB = AzerothVoicesDB or {}
    AzerothVoicesDB.minimapAngle = MINIMAP_DEFAULT_ANGLE
    AzerothVoicesDB.hideMinimap = false
    if self.minimapButton then
        self.minimapButton:Show()
        self:SetMinimapButtonPosition(MINIMAP_DEFAULT_ANGLE)
    end
    if self.frame then
        self.frame:ClearAllPoints()
        self.frame:SetPoint("CENTER", UIParent, "CENTER", 0, 0)
        self:SaveWindowPosition()
    end
    if DEFAULT_CHAT_FRAME and DEFAULT_CHAT_FRAME.AddMessage then
        DEFAULT_CHAT_FRAME:AddMessage("|cffffd100[AzerothVoices]|r Window and minimap positions reset to defaults.")
    end
end

function AzerothVoices:CreateMinimapButton()
    if self.minimapButton then return end
    local parentMinimap = Minimap or UIParent
    if not parentMinimap then return end

    local button = CreateFrame("Button", "AzerothVoicesMinimapButton", parentMinimap)
    button:SetWidth(31)
    button:SetHeight(31)
    button:SetFrameStrata("MEDIUM")
    button:SetFrameLevel(8)
    button:EnableMouse(true)
    button:SetMovable(true)
    button:RegisterForClicks("LeftButtonUp", "RightButtonUp")
    button:RegisterForDrag("LeftButton")
    button:SetHighlightTexture("Interface\\Minimap\\UI-Minimap-ZoomButton-Highlight")

    local background = button:CreateTexture(nil, "BACKGROUND")
    background:SetWidth(20)
    background:SetHeight(20)
    background:SetPoint("TOPLEFT", button, "TOPLEFT", 7, -5)
    background:SetTexture("Interface\\Minimap\\UI-Minimap-Background")
    button.background = background

    local icon = button:CreateTexture(nil, "ARTWORK")
    icon:SetWidth(18)
    icon:SetHeight(18)
    icon:SetPoint("TOPLEFT", button, "TOPLEFT", 7, -6)
    local iconTex = self:GetMinimapIconInfo()
    icon:SetTexture(iconTex)
    button.icon = icon

    local border = button:CreateTexture(nil, "OVERLAY")
    border:SetWidth(53)
    border:SetHeight(53)
    border:SetPoint("TOPLEFT", button, "TOPLEFT", 0, 0)
    border:SetTexture("Interface\\Minimap\\MiniMap-TrackingBorder")
    button.border = border

    local dragger = CreateFrame("Frame", nil, button)
    dragger:Hide()
    dragger:SetScript("OnUpdate", function()
        AzerothVoices:UpdateMinimapButtonPosition()
    end)
    button.dragger = dragger

    button:SetScript("OnClick", function()
        local isShift = IsShiftKeyDown and IsShiftKeyDown()
        local isAlt = IsAltKeyDown and IsAltKeyDown()
        local isCtrl = IsControlKeyDown and IsControlKeyDown()

        if isShift and arg1 == "LeftButton" then
            AzerothVoices:RegenAll()
            AzerothVoices:UpdateMinimapTooltip(this)
            return
        elseif isAlt and arg1 == "LeftButton" then
            AzerothVoices:RegenTraits()
            AzerothVoices:UpdateMinimapTooltip(this)
            return
        elseif isCtrl and arg1 == "LeftButton" then
            AzerothVoices:RegenTone()
            AzerothVoices:UpdateMinimapTooltip(this)
            return
        elseif isShift and arg1 == "RightButton" then
            AzerothVoices:RegenBackstory()
            AzerothVoices:UpdateMinimapTooltip(this)
            return
        elseif (isAlt or isCtrl) and arg1 == "RightButton" then
            AzerothVoices:OpenMinimapMenu(this)
            return
        end

        if arg1 == "RightButton" then
            AzerothVoices:CycleMinimapIcon()
            AzerothVoices:UpdateMinimapTooltip(this)
        else
            AzerothVoices:Toggle()
            AzerothVoices:UpdateMinimapTooltip(this)
        end
    end)

    button:SetScript("OnDragStart", function()
        this:LockHighlight()
        dragger:Show()
    end)

    button:SetScript("OnDragStop", function()
        this:UnlockHighlight()
        dragger:Hide()
        AzerothVoices:UpdateMinimapButtonPosition()
    end)

    button:SetScript("OnEnter", function()
        AzerothVoices:UpdateMinimapTooltip(this)
    end)

    button:SetScript("OnLeave", function()
        if GameTooltip then
            GameTooltip:Hide()
        end
    end)

    self.minimapButton = button
    self:SetMinimapButtonPosition()

    if AzerothVoicesDB and AzerothVoicesDB.hideMinimap then
        button:Hide()
    else
        button:Show()
    end
end

