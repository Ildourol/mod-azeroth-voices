-- Standalone Lua 5.0 compatibility & Mocked Vanilla 1.12 UI test runner
-- Verifies:
--   1. Static analysis: no #, no % modulo, no string.match/gmatch, no Wrath APIs.
--   2. Runtime simulation: TOC loading, PLAYER_LOGIN migration, slash commands,
--      roster loading, bot selection, trait editing, save confirmation, polling,
--      story regeneration, bulk forget, timeout, error handling, forged message rejection.

local function run_tests()
    local basePath = "client/AzerothVoices/"
    local files = { "AzerothVoices.lua", "AzerothVoicesRoster.lua", "AzerothVoicesUI.lua" }

    print("=== [Part 1] Static Analysis for Lua 5.0 & Vanilla 1.12 Compatibility ===")

    for _, filename in ipairs(files) do
        local path = basePath .. filename
        local f = io.open(path, "r")
        if not f then
            error("Could not open file: " .. path)
        end
        local content = f:read("*all")
        f:close()

        -- Check 1: Forbidden length operator '#'
        -- Must not appear anywhere outside strings/comments
        -- We do a thorough tokenized scan
        local inString = nil
        local inComment = false
        local inLongComment = false
        local lineNum = 1
        local colNum = 0
        local len = string.len(content)
        local i = 1

        while i <= len do
            local c = string.sub(content, i, i)
            local next2 = string.sub(content, i, i + 1)
            local next4 = string.sub(content, i, i + 3)

            if inComment then
                if c == "\n" then
                    inComment = false
                    lineNum = lineNum + 1
                    colNum = 0
                end
            elseif inLongComment then
                if next2 == "]]" then
                    inLongComment = false
                    i = i + 1
                elseif c == "\n" then
                    lineNum = lineNum + 1
                    colNum = 0
                end
            elseif inString then
                if c == "\\" then
                    i = i + 1 -- Skip escaped character
                elseif c == inString then
                    inString = nil
                elseif c == "\n" then
                    lineNum = lineNum + 1
                    colNum = 0
                end
            else
                if next4 == "--[[" then
                    inLongComment = true
                    i = i + 3
                elseif next2 == "--" then
                    inComment = true
                    i = i + 1
                elseif c == '"' or c == "'" then
                    inString = c
                elseif c == "\n" then
                    lineNum = lineNum + 1
                    colNum = 0
                elseif c == "#" then
                    error(string.format("Forbidden '#' operator found in %s at line %d!", filename, lineNum))
                elseif c == "%" then
                    -- Check if % is used as binary modulo operator
                    error(string.format("Forbidden unquoted '%%' operator found in %s at line %d!", filename, lineNum))
                end
            end
            i = i + 1
            colNum = colNum + 1
        end

        -- Check 2: Forbidden identifiers
        local forbiddenIdentifiers = {
            "string%.match",
            ":match%(",
            "string%.gmatch",
            ":gmatch%(",
            "HookScript",
            "ChatFrame_AddMessageEventFilter",
            "InterfaceOptions_AddCategory",
            "InterfaceOptionsFrame",
            "OnSizeChanged",
            "SetCursorPosition",
        }

        for _, pat in ipairs(forbiddenIdentifiers) do
            local s, e = string.find(content, pat)
            if s then
                error(string.format("Forbidden identifier '%s' found in %s at char %d!", pat, filename, s))
            end
        end

        print(string.format("  [OK] %s: No '#', '%%' binary, string.match, or Wrath APIs.", filename))
    end

    print("\n=== [Part 2] Mocked Vanilla 1.12 Runtime Simulation ===")

    -- Emulate Lua 5.0 built-in functions that were deprecated/removed in modern Lua (5.3+)
    if not table.getn then
        table.getn = function(t)
            if not t then return 0 end
            local n = 0
            while t[n + 1] ~= nil do
                n = n + 1
            end
            return n
        end
    end

    if not math.mod then
        math.mod = math.fmod or function(a, b) return a % b end
    end

    -- Environment setup
    local simTime = 100.0
    local sentMessages = {}
    local shownPopups = {}

    _G.GetTime = function()
        return simTime
    end

    _G.UnitName = function(unit)
        if unit == "player" then
            return "TestPlayer"
        end
        return "Unknown"
    end

    _G.SendChatMessage = function(msg, chan)
        table.insert(sentMessages, { msg = msg, chan = chan })
    end

    _G.DEFAULT_CHAT_FRAME = {
        AddMessage = function(self, msg)
            -- print("[ChatFrame] " .. msg)
        end
    }

    _G.GameTooltip = {
        SetOwner = function(self, owner, anchor) end,
        SetText = function(self, text) end,
        Show = function(self) end,
        Hide = function(self) end,
    }

    _G.StaticPopupDialogs = {}
    _G.SlashCmdList = {}
    _G.UISpecialFrames = {}

    -- Mock Frame creation
    local function createMockFrame(frameType, name, parent, template)
        local frame = {
            frameType = frameType,
            name = name,
            parent = parent,
            template = template,
            shown = false,
            points = {},
            scripts = {},
            enabled = true,
            width = 0,
            height = 0,
            text = "",
            textColor = { 1, 1, 1 },
            checked = false,
            value = 0,
            minVal = 0,
            maxVal = 0,
            scrollChild = nil,
            verticalScroll = 0,
            fontObject = nil,
        }

        function frame:SetWidth(w) self.width = w end
        function frame:SetHeight(h) self.height = h end
        function frame:GetWidth() return self.width end
        function frame:GetHeight() return self.height end

        function frame:SetPoint(point, rel, relPoint, x, y)
            table.insert(self.points, { point = point, rel = rel, relPoint = relPoint, x = x, y = y })
        end
        function frame:GetPoint(idx)
            local p = self.points[idx or 1]
            if p then
                return p.point, p.rel, p.relPoint, p.x, p.y
            end
            return "CENTER", nil, "CENTER", 0, 0
        end
        function frame:ClearAllPoints() self.points = {} end

        function frame:SetBackdrop(b) self.backdrop = b end
        function frame:SetBackdropColor(r, g, b, a) end
        function frame:SetBackdropBorderColor(r, g, b, a) end

        function frame:SetText(t) self.text = tostring(t or "") end
        function frame:GetText() return self.text end
        function frame:SetTextColor(r, g, b) self.textColor = { r, g, b } end
        function frame:GetTextColor() return self.textColor[1], self.textColor[2], self.textColor[3] end
        function frame:SetFontObject(fo) self.fontObject = fo end
        function frame:SetJustifyH(j) end
        function frame:SetJustifyV(j) end

        function frame:SetScript(name, func) self.scripts[name] = func end
        function frame:GetScript(name) return self.scripts[name] end
        function frame:FireScript(name, a1, a2, a3, a4)
            local func = self.scripts[name]
            if func then
                local prevThis = _G.this
                local prevEvent = _G.event
                local prevA1, prevA2, prevA3, prevA4 = _G.arg1, _G.arg2, _G.arg3, _G.arg4
                _G.this = self
                _G.event = name
                _G.arg1, _G.arg2, _G.arg3, _G.arg4 = a1, a2, a3, a4
                func()
                _G.this = prevThis
                _G.event = prevEvent
                _G.arg1, _G.arg2, _G.arg3, _G.arg4 = prevA1, prevA2, prevA3, prevA4
            end
        end

        function frame:Enable() self.enabled = true end
        function frame:Disable() self.enabled = false end
        function frame:IsEnabled() return self.enabled end

        function frame:Show()
            self.shown = true
            self:FireScript("OnShow")
        end
        function frame:Hide()
            self.shown = false
            self:FireScript("OnHide")
        end
        function frame:IsShown() return self.shown end
        function frame:Raise() end

        function frame:SetMinMaxValues(minV, maxV) self.minVal = minV; self.maxVal = maxV end
        function frame:GetMinMaxValues() return self.minVal, self.maxVal end
        function frame:SetValue(v)
            self.value = v
            self:FireScript("OnValueChanged")
        end
        function frame:GetValue() return self.value end
        function frame:SetValueStep(s) end
        function frame:SetOrientation(o) end
        function frame:SetThumbTexture(t) end

        function frame:SetScrollChild(c) self.scrollChild = c end
        function frame:SetVerticalScroll(s) self.verticalScroll = s end

        function frame:SetAutoFocus(af) end
        function frame:SetMaxLetters(ml) end
        function frame:SetFocus() end
        function frame:ClearFocus() end

        function frame:SetChecked(c) self.checked = not not c end
        function frame:GetChecked() return self.checked end

        function frame:SetHighlightTexture(t) end
        function frame:LockHighlight() end
        function frame:UnlockHighlight() end

        function frame:EnableMouseWheel(emw) end
        function frame:RegisterForDrag(d) end
        function frame:StartMoving() end
        function frame:StopMovingOrSizing() end
        function frame:SetClampedToScreen(c) end
        function frame:SetMovable(m) end
        function frame:EnableMouse(em) end

        function frame:RegisterEvent(e) end

        function frame:CreateFontString(name, layer, inherit)
            local fs = createMockFrame("FontString", name, self, inherit)
            function fs:GetStringHeight()
                local len = string.len(self.text or "")
                return math.max(12, math.floor(len / 40) * 14 + 14)
            end
            return fs
        end

        function frame:GetParent() return self.parent end

        if name then
            _G[name] = frame
        end

        return frame
    end

    _G.CreateFrame = createMockFrame
    _G.UIParent = createMockFrame("Frame", "UIParent", nil)

    _G.StaticPopup_Show = function(which, a1, a2)
        local dialog = _G.StaticPopupDialogs[which]
        table.insert(shownPopups, { which = which, a1 = a1, a2 = a2, dialog = dialog })
        return { which = which, data = dialog and dialog.targetData }
    end

    -- Load TOC files
    print("  Loading addon files in TOC order...")
    dofile(basePath .. "AzerothVoices.lua")
    dofile(basePath .. "AzerothVoicesRoster.lua")
    dofile(basePath .. "AzerothVoicesUI.lua")
    print("  [OK] Successfully loaded all Lua files without syntax or runtime error.")

    local eventFrame = _G.AzerothVoicesEventFrame
    assert(eventFrame, "AzerothVoicesEventFrame was not created!")

    local function fireEvent(eventName, a1, a2, a3, a4)
        local pThis = _G.this
        local pEvent = _G.event
        local p1, p2, p3, p4 = _G.arg1, _G.arg2, _G.arg3, _G.arg4
        _G.this = eventFrame
        _G.event = eventName
        _G.arg1, _G.arg2, _G.arg3, _G.arg4 = a1, a2, a3, a4
        eventFrame:GetScript("OnEvent")()
        _G.this = pThis
        _G.event = pEvent
        _G.arg1, _G.arg2, _G.arg3, _G.arg4 = p1, p2, p3, p4
    end

    local function sendAddonMsg(prefix, message, channel, sender)
        fireEvent("CHAT_MSG_ADDON", prefix, message, channel, sender)
    end

    -- 1. Test Migration from ChatterDB
    print("  [Test 1] Testing ChatterDB -> AzerothVoicesDB migration...")
    _G.ChatterDB = {
        point = "TOPLEFT",
        relPoint = "TOPLEFT",
        x = 120,
        y = -150,
        selectedGuid = 42,
    }
    _G.AzerothVoicesDB = nil

    -- Fire PLAYER_LOGIN
    fireEvent("PLAYER_LOGIN")

    assert(_G.AzerothVoicesDB, "AzerothVoicesDB was not initialized!")
    assert(_G.AzerothVoicesDB.migratedFromChatter == true, "migratedFromChatter flag not set!")
    assert(_G.AzerothVoicesDB.x == 120, "x position was not migrated!")
    assert(_G.AzerothVoicesDB.y == -150, "y position was not migrated!")
    assert(_G.AzerothVoicesDB.selectedGuid == 42, "selectedGuid was not migrated!")
    assert(eventFrame.frame, "AzerothVoicesMainFrame was not built!")
    print("  [OK] ChatterDB migrated accurately to AzerothVoicesDB.")

    -- 2. Test Slash Commands
    print("  [Test 2] Testing slash command toggling...")
    assert(_G.SlashCmdList["AZEROTHVOICES"], "SlashCmdList['AZEROTHVOICES'] missing!")
    sentMessages = {}
    _G.SlashCmdList["AZEROTHVOICES"]()

    assert(eventFrame.frame:IsShown() == true, "Frame did not show on toggle!")
    assert(table.getn(sentMessages) == 1, "Roster command was not sent on open!")
    assert(string.find(sentMessages[1].msg, "^%.avaddon 1 %d+ 1 1 roster$"), "Wrong command sent: " .. sentMessages[1].msg)
    print("  [OK] Slash command opened window and dispatched .avaddon roster request.")

    -- 3. Test Security Rejection of Malformed / Forged Messages
    print("  [Test 3] Testing security rejection of forged / non-matching addon messages...")

    -- Wrong prefix
    sendAddonMsg("CHATTER_ADDON", "1\treq1\t1\t1\t1\tROSTER_BEGIN", "GUILD", "TestPlayer")
    assert(eventFrame.pendingRoster ~= nil and table.getn(eventFrame.pendingRoster) == 0, "Wrong prefix accepted!")

    -- Wrong channel
    sendAddonMsg("AZEROTH_VOICES", "1\treq1\t1\t1\t1\tROSTER_BEGIN", "WHISPER", "TestPlayer")
    assert(table.getn(eventFrame.pendingRoster) == 0, "Wrong channel accepted!")

    -- Forged sender
    sendAddonMsg("AZEROTH_VOICES", "1\treq1\t1\t1\t1\tROSTER_BEGIN", "GUILD", "EvePlayer")
    assert(table.getn(eventFrame.pendingRoster) == 0, "Forged sender accepted!")
    print("  [OK] Non-matching prefixes, channels, and forged senders strictly rejected.")

    -- 4. Test Inbound Multi-part Reassembly & Roster Processing
    print("  [Test 4] Testing inbound frame reassembly and roster handling...")
    -- Message 1: ROSTER_BEGIN
    sendAddonMsg("AZEROTH_VOICES", "1\treq1\t1\t1\t1\tROSTER_BEGIN", "GUILD", "TestPlayer")
    assert(eventFrame.pendingRoster ~= nil, "pendingRoster was not initialized!")

    -- Message 2: ROSTER 101 BotAlpha (split into 2 parts; deliver part 2 before part 1)
    sendAddonMsg("AZEROTH_VOICES", "1\treq1\t2\t2\t2\t1 BotAlpha", "GUILD", "TestPlayer")
    assert(table.getn(eventFrame.pendingRoster) == 0, "Incomplete multi-part prematurely added entry!")

    sendAddonMsg("AZEROTH_VOICES", "1\treq1\t2\t1\t2\tROSTER 10", "GUILD", "TestPlayer")
    assert(table.getn(eventFrame.pendingRoster) == 1, "Reassembled entry not added to pendingRoster!")

    -- Message 3: ROSTER 102 BotBeta
    sendAddonMsg("AZEROTH_VOICES", "1\treq1\t3\t1\t1\tROSTER 102 BotBeta", "GUILD", "TestPlayer")
    assert(table.getn(eventFrame.pendingRoster) == 2, "Second entry not added to pendingRoster!")

    -- Message 4: ROSTER_END
    sendAddonMsg("AZEROTH_VOICES", "1\treq1\t4\t1\t1\tROSTER_END", "GUILD", "TestPlayer")
    assert(table.getn(eventFrame.roster) == 2, "Roster count should be 2!")
    assert(eventFrame.roster[1].name == "BotAlpha", "BotAlpha sorting check")
    assert(eventFrame.roster[2].name == "BotBeta", "BotBeta sorting check")
    -- Bot 101 should be selected automatically
    assert(eventFrame.selectedGuid == 101, "First bot should be selected!")
    print("  [OK] Multi-part frames reassembled out of order; roster loaded and sorted.")

    -- 5. Test Profile Reception and Trait Editing
    print("  [Test 5] Testing profile reception and trait editing...")
    sentMessages = {}
    -- Deliver profile for bot 101
    sendAddonMsg("AZEROTH_VOICES", "1\treq2\t1\t1\t1\tPROFILE 101 BotAlpha Friendly Loyal Cheerful Warm", "GUILD", "TestPlayer")
    sendAddonMsg("AZEROTH_VOICES", "1\treq2\t2\t1\t1\tBACKSTORY 101 A%20brave%20paladin%20from%20Stormwind.", "GUILD", "TestPlayer")

    local p = eventFrame.frame
    assert(p.trait1:GetText() == "Friendly", "Trait 1 mismatch: " .. p.trait1:GetText())
    assert(p.trait2:GetText() == "Loyal", "Trait 2 mismatch: " .. p.trait2:GetText())
    assert(p.trait3:GetText() == "Cheerful", "Trait 3 mismatch: " .. p.trait3:GetText())
    assert(p.tone:GetText() == "Warm", "Tone mismatch: " .. p.tone:GetText())
    assert(p.backstory:GetText() == "A brave paladin from Stormwind.", "Backstory mismatch: " .. p.backstory:GetText())
    assert(p.saveBtn:IsEnabled() == false, "Save button should be disabled before edits!")

    -- Edit trait 1
    p.trait1:SetText("Courageous")
    p.trait1:FireScript("OnTextChanged")
    assert(p.saveBtn:IsEnabled() == true, "Save button should be enabled after edits!")

    -- Click Save
    shownPopups = {}
    p.saveBtn:FireScript("OnClick")
    assert(table.getn(shownPopups) == 1, "Save confirmation popup should show!")
    assert(shownPopups[1].which == "AZEROTHVOICES_CONFIRM_SAVE_TRAITS", "Popup dialog name check")

    -- Accept popup
    sentMessages = {}
    _G.StaticPopupDialogs["AZEROTHVOICES_CONFIRM_SAVE_TRAITS"].OnAccept()
    assert(table.getn(sentMessages) == 1, "Save command should be sent!")
    assert(string.find(sentMessages[1].msg, "set 101 Courageous Loyal Cheerful"), "Save payload check: " .. sentMessages[1].msg)

    -- Simulate server UPDATED response with changed=true
    sendAddonMsg("AZEROTH_VOICES", "1\treq3\t1\t1\t1\tUPDATED 101 BotAlpha changed", "GUILD", "TestPlayer")
    assert(eventFrame.pendingToneGuid == 101, "Tone poll should be started!")
    assert(eventFrame.pendingBackstoryGuid == 101, "Backstory poll should be started!")

    -- Advance time by 2.1s and tick OnUpdate
    simTime = simTime + 2.1
    sentMessages = {}
    local pThis = _G.this
    local p1 = _G.arg1
    _G.this = eventFrame
    _G.arg1 = 2.1
    eventFrame:GetScript("OnUpdate")()
    _G.this = pThis
    _G.arg1 = p1

    assert(table.getn(sentMessages) >= 1, "Poll commands should be dispatched!")
    assert(string.find(sentMessages[1].msg, "get 101"), "Poll command check: " .. sentMessages[1].msg)

    -- Return updated profile & backstory
    sendAddonMsg("AZEROTH_VOICES", "1\treq4\t1\t1\t1\tPROFILE 101 BotAlpha Courageous Loyal Cheerful Resolute", "GUILD", "TestPlayer")
    sendAddonMsg("AZEROTH_VOICES", "1\treq4\t2\t1\t1\tBACKSTORY 101 A%20seasoned%20veteran.", "GUILD", "TestPlayer")
    assert(eventFrame.pendingToneGuid == nil, "Tone poll should have completed!")
    assert(eventFrame.pendingBackstoryGuid == nil, "Backstory poll should have completed!")
    print("  [OK] Profile editing, save confirmation, and polling cycle verified.")

    -- 6. Test Story Regeneration
    print("  [Test 6] Testing story regeneration...")
    sentMessages = {}
    p.regenStoryBtn:FireScript("OnClick")
    assert(table.getn(sentMessages) == 1, "regenbackstory command should be sent!")
    assert(string.find(sentMessages[1].msg, "regenbackstory 101"), "regenbackstory command check")

    sendAddonMsg("AZEROTH_VOICES", "1\treq5\t1\t1\t1\tBACKSTORY_REGEN 101 BotAlpha", "GUILD", "TestPlayer")
    assert(eventFrame.pendingBackstoryGuid == 101, "Backstory poll should be active!")

    sendAddonMsg("AZEROTH_VOICES", "1\treq5\t2\t1\t1\tBACKSTORY 101 A%20regenerated%20legendary%20champion.", "GUILD", "TestPlayer")
    assert(eventFrame.pendingBackstoryGuid == nil, "Backstory poll should complete!")
    assert(p.backstory:GetText() == "A regenerated legendary champion.", "Updated backstory check")
    print("  [OK] Story regeneration cycle verified.")

    -- 7. Test Bulk Forget Flow
    print("  [Test 7] Testing bulk forget flow...")
    eventFrame.checkedBots[101] = true
    eventFrame.checkedBots[102] = true
    eventFrame:UpdateRosterViews()

    shownPopups = {}
    p.forgetBtn:FireScript("OnClick")
    assert(table.getn(shownPopups) == 1, "Forget popup should show!")
    assert(shownPopups[1].which == "AZEROTHVOICES_CONFIRM_FORGET", "Popup name check")
    assert(shownPopups[1].a1 == 2, "Target count should be 2!")

    -- Accept popup
    sentMessages = {}
    _G.StaticPopupDialogs["AZEROTHVOICES_CONFIRM_FORGET"].OnAccept()
    assert(eventFrame.forgetQueue ~= nil, "Forget queue should be active!")

    -- Tick OnUpdate to send first forget
    simTime = simTime + 0.4
    _G.this = eventFrame
    _G.arg1 = 0.4
    eventFrame:GetScript("OnUpdate")()
    assert(table.getn(sentMessages) == 1, "First forget command should be sent!")
    assert(string.find(sentMessages[1].msg, "forget 101"), "forget 101 check: " .. sentMessages[1].msg)

    -- Acknowledge 101
    sendAddonMsg("AZEROTH_VOICES", "1\treq6\t1\t1\t1\tFORGOTTEN 101", "GUILD", "TestPlayer")
    assert(eventFrame.forgetDone == 1, "forgetDone should be 1!")

    -- Tick OnUpdate to send second forget
    sentMessages = {}
    simTime = simTime + 0.4
    _G.this = eventFrame
    _G.arg1 = 0.4
    eventFrame:GetScript("OnUpdate")()
    assert(table.getn(sentMessages) == 1, "Second forget command should be sent!")
    assert(string.find(sentMessages[1].msg, "forget 102"), "forget 102 check: " .. sentMessages[1].msg)

    -- Acknowledge 102
    sendAddonMsg("AZEROTH_VOICES", "1\treq7\t1\t1\t1\tFORGOTTEN 102", "GUILD", "TestPlayer")
    assert(eventFrame.forgetQueue == nil, "Forget queue should have completed!")
    print("  [OK] Bulk forget queue executed and completed smoothly.")

    -- 8. Test Timeout & Error Handling
    print("  [Test 8] Testing timeout and error handling...")
    eventFrame:RequestRoster()
    assert(eventFrame.pendingRoster ~= nil, "pendingRoster should be active!")

    -- Advance time 10.5 seconds
    simTime = simTime + 10.5
    _G.this = eventFrame
    _G.arg1 = 10.5
    eventFrame:GetScript("OnUpdate")()
    assert(eventFrame.pendingRoster == nil, "pendingRoster should have timed out!")
    assert(string.find(p.status:GetText(), "timed out"), "Status text should indicate timeout: " .. p.status:GetText())

    -- Send server error payload
    sendAddonMsg("AZEROTH_VOICES", "1\treq8\t1\t1\t1\tERROR llmc Unknown_bot_guid", "GUILD", "TestPlayer")
    assert(string.find(p.status:GetText(), "Unknown_bot_guid"), "Error text should be shown in status: " .. p.status:GetText())
    print("  [OK] Timeout and server error handling verified.")

    -- 9. Test Window Position Persistence
    print("  [Test 9] Testing window position persistence...")
    p:ClearAllPoints()
    p:SetPoint("CENTER", _G.UIParent, "CENTER", 250, -180)

    -- Fire PLAYER_LOGOUT
    local pEvent = _G.event
    local pThis = _G.this
    _G.this = eventFrame
    _G.event = "PLAYER_LOGOUT"
    eventFrame:GetScript("OnEvent")()
    _G.this = pThis
    _G.event = pEvent

    assert(_G.AzerothVoicesDB.x == 250, "Position x not saved: " .. tostring(_G.AzerothVoicesDB.x))
    assert(_G.AzerothVoicesDB.y == -180, "Position y not saved: " .. tostring(_G.AzerothVoicesDB.y))
    print("  [OK] Window coordinates successfully persisted to AzerothVoicesDB.")

    print("\nALL ADDON LUA 5.0 & RUNTIME SIMULATION TESTS PASSED SUCCESSFULLY!\n")
end

run_tests()
