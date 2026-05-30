-- EvoFaderWing 0.4 OSC script for syncing EVOFaderWing using OSC
-- Sends execUpdate (page + faders + exec status) and colorUpdate for all Execs and Faders
-- OSC sent when data has changed or after autoResendInterval
--
-- This refactor uses a cache + hook model for faster updates (~30 to 60 times more efficient)
--
-- Executor proxy relationships are fully supported.
--
-- Special thanks to xxpasixx for his pam-osc code which I modified for my project
-- GPL3

-- grandMA3 passes plugin/component context in the Lua component chunk varargs.
local CHUNK_PLUGIN_NAME = select(1, ...)
local CHUNK_COMPONENT_NAME = select(2, ...)
local CHUNK_SIGNAL_TABLE = select(3, ...)
local CHUNK_LUA_COMPONENT_HANDLE = select(4, ...)

local function StartGui()
    local isRunning = GetVar(GlobalVars(), "EvoFaderWingRunning") == true

    local descTable = {
        title = "EvoFaderWing0.4 OSC Control",
        caller = GetFocusDisplay(),
        add_args = {FilterSupport = "Yes"},
    }

    if isRunning then
        descTable.items = {"Stop", "Force Reload"}
        descTable.message = "EvoFaderWing0.4 OSC is RUNNING"
    else
        descTable.items = {"Start", "Force Reload"}
        descTable.message = "EvoFaderWing0.4 OSC is STOPPED"
    end

    local index, name = PopupInput(descTable)
    return index, name
end

function main(...)
    local index, name = StartGui()

    if name == "Start" then
        Printf("Starting EvoFaderWing0.4 OSC...")

        SetVar(GlobalVars(), "EvoFaderWingRunning", true)

        local pluginHandle = nil
        local pluginLookupSource = "none"
        local argCount = select("#", ...)
        if CHUNK_LUA_COMPONENT_HANDLE ~= nil then
            local okParent, parent = pcall(function() return CHUNK_LUA_COMPONENT_HANDLE:Parent() end)
            if okParent and parent ~= nil then
                pluginHandle = parent
                pluginLookupSource = "chunk-parent-depth=1"
            end
        end

        local executorsToWatch = {}
        local oldValues = {}
        local oldColorValues = {}
        local oldExecStatus = {}

        local oscEntryName = "EvoFaderWing"

        local function sendOsc(message)
            local feedback = Cmd('SendOSC "' .. oscEntryName .. '" "' .. message .. '"')
            if feedback ~= "OK" then
                Printf('SendOSC failed for target "' .. oscEntryName .. '": ' .. tostring(feedback))
            end

            return feedback
        end

        local function isDeskLocked()
            local ok, result = pcall(function() return DeskLocked() end)
            if ok and result ~= nil then
                return result == true or result == 1
            end
            return false
        end

        -- CMD status bitmask flags (must match WING_STATUS_* defines in Config.h)
        -- Sent via /wingStatus as second argument. Desk lock is the first argument (0 or 1).
        local WING_STATUS_CMD_MODE      = 2   -- bit 1: MA3 cmd line active, add executor to cmdline
        local WING_STATUS_CMD_EXEC_MODE = 4   -- bit 2: MA3 cmd line active, add + auto-execute
        local WING_STATUS_CMD_COPY_SRC  = 8   -- bit 3: copy/move with source already selected
        local WING_STATUS_CMD_THRU      = 16  -- bit 4: thru active, send only exec number (no Page X.Y)

        -- CMD keyword table: true = intercept + Execute Yes, false = intercept + Execute No.
        -- Keywords not listed here = no interception, button behaves normally.
        -- copy and move are handled separately below (context-aware behaviour).
        local CMD_KEYWORDS = {
            store=true, delete=true, fix=true,
            update=true, on=true, off=true,
            toggle=true, release=true, rel=true,
            load=true, select=true, go=true,
            top=true, temp=true, flash=true,   
            deactivate=true, kill=true, activate=true,
            lock=true, label=true, edit=true, 
            assign=true
        }

        -- Returns the wingStatus CMD flags for the current MA3 command line.
        -- WING_STATUS_CMD_EXEC_MODE: CMD_KEYWORDS[keyword] == true  -> Execute Yes
        -- WING_STATUS_CMD_MODE:      CMD_KEYWORDS[keyword] == false -> Execute No, user confirms
        -- WING_STATUS_CMD_COPY_SRC:  copy/move with source already selected -> Wing decides + prefix
        -- 0:                         keyword not listed or empty    -> no interception
        local function getCmdFlags()
            local ok, text = pcall(function()
                local cmd = CmdObj()
                return cmd and cmd.cmdtext or ""
            end)
            if not ok or type(text) ~= "string" or text == "" then return 0 end

            -- If cmdline ends with "At" (destination prompt), always send Page X.Y + Execute
            if string.match(string.lower(text), "%sat%s*$") then
                return WING_STATUS_CMD_EXEC_MODE
            end

            local keyword = string.lower(string.match(text, "^%s*(%a+)") or "")
            if keyword == "" then return 0 end

            -- copy/move: context-aware — detect whether source is already on the cmdline
            -- | State                          | Wing status        | C++ result                        |
            -- | copy/move, no args             | CMD_MODE           | Page X.Y added, no execute        |
            -- | copy/move + thru in cmdline    | CMD_MODE           | Page X.Y added, no execute        |
            -- | copy/move, source selected     | CMD_COPY_SRC       | empty→At Page X.Y+Enter           |
            -- |                               |                    | occupied→+ Page X.Y, no execute   |
            if keyword == "copy" or keyword == "move" then
                local rest = string.match(text, "^%s*%a+%s+(.-)%s*$") or ""
                if rest ~= "" then
                    -- thru handling:
                    --   "Copy Page 1.X Thru"          → thru open, send exec# only, no execute
                    --   "Copy Page 1.X Thru Page 1.Y" → thru complete, send exec# only + execute
                    if string.find(string.lower(rest), "thru", 1, true) then
                        local afterThru = string.match(string.lower(rest), "thru%s+(%S+)")
                        if afterThru and afterThru ~= "" then
                            return WING_STATUS_CMD_COPY_SRC  -- range complete, next = At Page X.Y + execute
                        else
                            return WING_STATUS_CMD_THRU      -- open range, send exec# only, no execute
                        end
                    end
                    return WING_STATUS_CMD_COPY_SRC  -- source selected, Wing checks target occupancy
                else
                    return WING_STATUS_CMD_MODE      -- just "Copy"/"Move", add page without execute
                end
            end

            local entry = CMD_KEYWORDS[keyword]
            if entry == nil then return 0 end
            return entry and WING_STATUS_CMD_EXEC_MODE or WING_STATUS_CMD_MODE
        end

        local execRanges = {
            {101, 110},
            {201, 210},
            {301, 310},
            {401, 410},
        }

        for _, range in ipairs(execRanges) do
            for i = range[1], range[2] do
                executorsToWatch[#executorsToWatch + 1] = i
                oldExecStatus[i] = 0
                oldColorValues[i] = "0,0,0,0"
            end
        end

        for i = 201, 210 do
            oldValues[i] = "000"
        end

        for i = 101, 110 do oldColorValues[i] = "0,0,0,0" end
        for i = 201, 210 do oldColorValues[i] = "0,0,0,0" end
        for i = 301, 310 do oldColorValues[i] = "0,0,0,0" end
        for i = 401, 410 do oldColorValues[i] = "0,0,0,0" end

        local tick = 1 / 50
        local ticksPerSecond = (tick > 0) and (1 / tick) or 50
        local function ticksToSeconds(ticks)
            return ticks / ticksPerSecond
        end
        local resendTick = 0
        local refreshTick = 0
        local autoResendInterval = 500
        local autoRefreshInterval = 25
        local execUpdateTypeTag = "," .. string.rep("i", 1 + 10 + #executorsToWatch)
        local colorUpdateTypeTag = "," .. "i" .. string.rep("s", #executorsToWatch)
        local DEBUG_PROXY_LINK = false

        local execMetaCache = {}
        local execMetaPageIndex = -1

        -- Returns "R,G,B,A" (0-255) from the MA3 theme for the given pool object's type.
        -- Primary path: Root/ColorTheme/ColorGroups/PoolDefault/<TypeName> (singular, e.g. "Group", "Master")
        -- The typeName is extracted from tostring(obj) = "Group 18" -> "Group", matching PoolDefault keys directly.
        -- Falls back to a full ColorGroups scan if PoolDefault doesn't have the key.
        -- Result is cached per typeName to avoid repeated tree walks at 50 Hz.
        local themeColorCache = {}
        local function extractThemeColorString(obj)
            if obj == nil then return nil end
            local okStr, str = pcall(function() return tostring(obj) end)
            if not okStr or type(str) ~= "string" then return nil end
            local typeName = string.match(str, "^(%a+)%s+")
            if typeName == nil then return nil end
            -- Return cached result (false = confirmed not found)
            if themeColorCache[typeName] ~= nil then
                return themeColorCache[typeName] ~= false and themeColorCache[typeName] or nil
            end
            local ok1, root = pcall(function() return Root() end)
            if not ok1 or root == nil then return nil end

            local function parseRGBA(e)
                if e == nil then return nil end
                local okR, rgba = pcall(function() return e["RGBA"] end)
                if not okR or rgba == nil or type(rgba) ~= "string" or #rgba ~= 8 then return nil end
                local r = tonumber(rgba:sub(1,2), 16)
                local g = tonumber(rgba:sub(3,4), 16)
                local b = tonumber(rgba:sub(5,6), 16)
                local a = tonumber(rgba:sub(7,8), 16)
                if r == nil or g == nil or b == nil or a == nil then return nil end
                return r .. "," .. g .. "," .. b .. "," .. a
            end

            -- Fast path: PoolDefault contains all pool type defaults with singular keys
            local okPD, pd = pcall(function()
                return root["ColorTheme"]["ColorGroups"]["PoolDefault"]
            end)
            if okPD and pd ~= nil then
                local okE, e = pcall(function() return pd[typeName] end)
                if okE and e ~= nil then
                    local result = parseRGBA(e)
                    if result ~= nil then
                        themeColorCache[typeName] = result
                        return result
                    end
                end
            end

            -- Fallback: search all ColorGroups children (plural and singular keys)
            local okCG, cg = pcall(function() return root["ColorTheme"]["ColorGroups"] end)
            if okCG and cg ~= nil then
                for ci = 1, 100 do
                    local okChild, child = pcall(function() return cg[ci] end)
                    if not okChild or child == nil then break end
                    for _, key in ipairs({ typeName .. "s", typeName }) do
                        local okE, e = pcall(function() return child[key] end)
                        if okE and e ~= nil then
                            local result = parseRGBA(e)
                            if result ~= nil then
                                themeColorCache[typeName] = result
                                return result
                            end
                        end
                    end
                end
            end

            themeColorCache[typeName] = false  -- cache miss
            return nil
        end

        local function extractAppearance(target)
            if target == nil then return nil end
            local ok, ap = pcall(function() return target["APPEARANCE"] end)
            if ok and ap ~= nil then return ap end
            return nil
        end

        local function extractCueAppearance(seqObj)
            if seqObj == nil then return nil end
            if seqObj.preferCueAppearance ~= true then return nil end
            local ok, child = pcall(function() return seqObj:CurrentChild() end)
            if not ok or child == nil then return nil end
            local ok2, ap = pcall(function() return child[1] and child[1].Appearance end)
            if ok2 and ap ~= nil then return ap end
            return nil
        end

        local function getName(obj)
            if obj == nil then return "nil" end
            local ok, res = pcall(function() return obj:Name() end)
            if ok and res ~= nil then return res end
            local ok2, res2 = pcall(function() return obj.Name end)
            if ok2 and res2 ~= nil then return res2 end
            return "?"
        end

        local function getType(obj)
            local ok, res = pcall(function() return obj:Type() end)
            if ok then return res end
            return nil
        end

        local function lowerType(obj)
            local t = getType(obj)
            if t == nil then
                return ""
            end
            return string.lower(tostring(t))
        end

        -- Navigates to the DataPool entry for any object whose tostring() is "TypeName Index"
        -- (e.g. "Group 18", "Sequence 5", "Macro 3") and returns its APPEARANCE, or nil.
        local function extractPoolAppearance(obj)
            if obj == nil then return nil end
            local okStr, str = pcall(function() return tostring(obj) end)
            if not okStr or type(str) ~= "string" then return nil end
            local typeName, idx = string.match(str, "^(%a+)%s+(%d+)$")
            if typeName == nil or idx == nil then return nil end
            idx = tonumber(idx)
            local poolKey = typeName .. "s"  -- "Group" -> "Groups", "Sequence" -> "Sequences"
            local okPool, pool = pcall(function() return DataPool() end)
            if not okPool or pool == nil then return nil end
            local ok2, poolColl = pcall(function() return pool[poolKey] end)
            if not ok2 or poolColl == nil then return nil end
            local ok3, entry = pcall(function() return poolColl[idx] end)
            if not ok3 or entry == nil then return nil end
            local ok4, ap = pcall(function() return entry["APPEARANCE"] end)
            if ok4 and ap ~= nil then return ap end
            return nil
        end


        local function isSequence(obj)
            if obj == nil then return false end
            local t = lowerType(obj)
            if t == "sequence" then return true end
            local n = getName(obj)
            if n and string.find(string.lower(tostring(n)), "sequence", 1, true) then
                return true
            end
            return false
        end

        local function parseExecReference(execRef)
            if execRef == nil then
                return nil, nil
            end

            local ref = tostring(execRef)
            if ref == "" or ref == "nil" then
                return nil, nil
            end

            local pageIndex = string.match(ref, "Page%s+(%d+)")
            local execNo = string.match(ref, "[%. ](%d+)$")
            if execNo == nil then
                execNo = string.match(ref, "Executor%s+(%d+)")
            end

            return pageIndex and tonumber(pageIndex) or nil, execNo and tonumber(execNo) or nil
        end

        local function resolvePlaybackOn(obj)
            if obj == nil then
                return false
            end

            local ok, activePlayback = pcall(function() return obj:HasActivePlayback() end)
            if ok and activePlayback ~= nil then
                return activePlayback == true or activePlayback == 1
            end

            if obj["isOn"] ~= nil then
                return obj["isOn"] ~= 0
            elseif obj["IsOn"] ~= nil then
                return obj["IsOn"] ~= 0
            elseif obj["RUNNING"] ~= nil then
                return obj["RUNNING"] ~= 0
            elseif obj["Running"] ~= nil then
                return obj["Running"] ~= 0
            end
            return false
        end

        local function rebuildExecMetaCache(pageIndex)
            local function dbgProxy(fmt, ...)
                if DEBUG_PROXY_LINK then
                    Printf("[ProxyDebug] " .. string.format(fmt, ...))
                end
            end

            dbgProxy("rebuild start page=%s", tostring(pageIndex))

            for _, execNo in ipairs(executorsToWatch) do
                local handle, handlePage = GetExecutor(execNo)
                local handleObj = handle and handle.Object or nil
                local proxyExecRef = handle and handle["EXEC"] or nil
                local proxyPageIndex, proxyHostExecNo = parseExecReference(proxyExecRef)
                local proxyHostHandle = nil
                local proxyHostPage = nil
                local proxyHostObject = nil
                local isProxy = false

                if proxyHostExecNo ~= nil and proxyHostExecNo ~= execNo then
                    isProxy = true
                    if proxyPageIndex == nil or proxyPageIndex == pageIndex then
                        proxyHostHandle, proxyHostPage = GetExecutor(proxyHostExecNo)
                        proxyHostObject = proxyHostHandle and proxyHostHandle.Object or nil
                    end
                end

                local seqObj = nil
                if isSequence(handleObj) then
                    seqObj = handleObj
                elseif isSequence(proxyHostObject) then
                    seqObj = proxyHostObject
                elseif isSequence(proxyHostHandle) then
                    seqObj = proxyHostHandle
                elseif isSequence(handle) then
                    seqObj = handle
                end

                local meta = {
                    handle = handle,
                    handlePage = handlePage,
                    handleObject = handleObj,
                    proxyExecRef = proxyExecRef,
                    proxyPageIndex = proxyPageIndex,
                    proxyHostExecNo = proxyHostExecNo,
                    proxyHostHandle = proxyHostHandle,
                    proxyHostPage = proxyHostPage,
                    proxyHostObject = proxyHostObject,
                    primaryObject = proxyHostObject or handleObj or proxyHostHandle or handle,
                    sequenceObject = seqObj,
                    sequenceHostObject = seqObj and (proxyHostObject or seqObj) or nil,
                    sequenceHostExecNo = seqObj and (proxyHostExecNo or execNo) or nil,
                    isSequenceProxy = isProxy and seqObj ~= nil,
                    isMacroPluginProxy = isProxy and seqObj == nil and proxyHostExecNo ~= nil,
                    isPopulated = handle ~= nil and ((handleObj ~= nil) or (proxyHostHandle ~= nil) or (seqObj ~= nil) or (proxyExecRef ~= nil)),
                }

                if isProxy then
                    dbgProxy(
                        "proxy exec=%d host=%s page=%s seq=%s macroPlugin=%s",
                        execNo,
                        tostring(proxyHostExecNo),
                        tostring(proxyPageIndex),
                        tostring(meta.isSequenceProxy),
                        tostring(meta.isMacroPluginProxy)
                    )
                else
                    dbgProxy("direct exec=%d seq=%s", execNo, tostring(seqObj ~= nil))
                end

                execMetaCache[execNo] = meta
            end

            execMetaPageIndex = pageIndex
            dbgProxy("rebuild done page=%s", tostring(pageIndex))
        end

        local function ensureExecMetaCache(pageIndex, refreshDetails, execDirtyFlags)
            local anyDirty = false
            if execDirtyFlags ~= nil then
                for _, execNo in ipairs(executorsToWatch) do
                    if execDirtyFlags[execNo] then
                        anyDirty = true
                        break
                    end
                end
            end

            if execMetaPageIndex ~= pageIndex or refreshDetails or anyDirty then
                rebuildExecMetaCache(pageIndex)
            end
        end

        local function readExecutorState(execNo, wantsValue, wantsColor)
            local meta = execMetaCache[execNo]
            if meta == nil or meta.handle == nil then
                return 0, "0,0,0,0", 0
            end

            local faderValue = 0
            if wantsValue then
                local okFader, fv = pcall(function() return meta.handle:GetFader({token = "FaderMaster", faderDisabled = false}) end)
                if okFader and fv then
                    faderValue = fv
                end
            end

            local colorValue = "0,0,0,0"
            if wantsColor then
                local ap = extractCueAppearance(meta.sequenceObject)
                    or extractAppearance(meta.sequenceObject)
                    or extractAppearance(meta.sequenceHostObject)
                    or extractAppearance(meta.primaryObject)
                    or extractAppearance(meta.handle)
                    or extractPoolAppearance(meta.handleObject)
                    or extractPoolAppearance(meta.primaryObject)
                if ap ~= nil then
                    colorValue = ap["BACKR"] .. "," .. ap["BACKG"] .. "," .. ap["BACKB"] .. "," .. ap["BACKALPHA"]
                else
                    colorValue = extractThemeColorString(meta.handleObject)
                        or extractThemeColorString(meta.primaryObject)
                        or "255,255,255,255"
                end
            end

            local statusObj = meta.sequenceHostObject or meta.primaryObject
            local isOn = resolvePlaybackOn(statusObj)
            local statusCode = 0
            if meta.isPopulated then
                statusCode = isOn and 2 or 1
            end

            return faderValue, colorValue, statusCode
        end

        local function collectExecState(refreshDetails, execDirtyFlags, currentPageIndex)
            local currentFaderValues = {}
            local currentColorValues = {}
            local currentExecStatus = {}

            ensureExecMetaCache(currentPageIndex, refreshDetails, execDirtyFlags)

            local faderDataChanged = false
            local statusChanged = false
            local execColorChanged = false
            local colorChanged = false

            for _, execNo in ipairs(executorsToWatch) do
                local wantsValue = (execNo >= 201 and execNo <= 210)
                local refreshThis = refreshDetails or (execDirtyFlags and execDirtyFlags[execNo] == true)
                local wantsColorThis = refreshThis

                local faderValue, colorValue, statusCode = readExecutorState(execNo, wantsValue, wantsColorThis)
                if refreshThis and execDirtyFlags then
                    execDirtyFlags[execNo] = false
                end

                if wantsValue then
                    currentFaderValues[execNo] = faderValue
                    if oldValues[execNo] ~= faderValue then
                        faderDataChanged = true
                    end
                end

                if wantsColorThis then
                        currentColorValues[execNo] = colorValue
                        if oldColorValues[execNo] ~= colorValue then
                            colorChanged = true
                            if (execNo >= 101 and execNo <= 110) or (execNo >= 201 and execNo <= 210) then
                                faderDataChanged = true
                            else
                                execColorChanged = true
                            end
                        end
                    else
                        currentColorValues[execNo] = oldColorValues[execNo] or colorValue
                    end

                currentExecStatus[execNo] = statusCode
                if oldExecStatus[execNo] ~= statusCode then
                    statusChanged = true
                end
            end

            return {
                faderDataChanged = faderDataChanged,
                statusChanged = statusChanged,
                execColorChanged = execColorChanged,
                colorChanged = colorChanged,
                currentFaderValues = currentFaderValues,
                currentColorValues = currentColorValues,
                currentExecStatus = currentExecStatus
            }
        end

        Printf("start EvoFaderWing0.4 OSC - fader values/colors + executor status (101-410)")
        if autoResendInterval > 0 then
            Printf(string.format("autoResendInterval: %d (every %.2f seconds)", autoResendInterval, ticksToSeconds(autoResendInterval)))
        else
            Printf("autoResendInterval: disabled")
        end
        if autoRefreshInterval > 0 then
            Printf(string.format("autoRefreshInterval: %d (every %.2f seconds)", autoRefreshInterval, ticksToSeconds(autoRefreshInterval)))
        else
            Printf("autoRefreshInterval: disabled")
        end

        local startPage = CurrentExecPage()
        local destPage = 1
        if startPage and startPage.index ~= nil then
            destPage = startPage.index
        end

        local forceReload = true
        local stateDirty = true
        local pageDirty = true
        local execDirty = {}
        local lastPageIndex = destPage
        local lastSentPageUpdate = nil
        local PAGE_SETTLE_GUARD_TICKS = 3
        local pageSettleGuardTicks = 0
        local lastDeskLockState = isDeskLocked()
        local deskLockChanged = false
        local lastCmdFlags = 0
        local cmdFlagsChanged = false

        local HOOK_DEBUG = false
        local hasHookObjectChange = type(HookObjectChange) == "function"
        local hasUnhook = type(Unhook) == "function"
        local hasPluginHandle = type(pluginHandle) == "userdata"
        local pluginHandleType = "nil"
        if hasPluginHandle then
            local okType, t = pcall(function() return pluginHandle:Type() end)
            if okType and t ~= nil then
                pluginHandleType = tostring(t)
            else
                pluginHandleType = "userdata"
            end
        end
        local hooksEnabled = hasHookObjectChange and hasUnhook and hasPluginHandle
        local hookIds = {}

        local function sendImmediatePageUpdate(pageIndex)
            if pageIndex == nil or pageIndex == lastSentPageUpdate then
                return
            end
            local pageMessage = "/updatePage/current,i," .. pageIndex
            sendOsc(pageMessage)
            lastSentPageUpdate = pageIndex
            if HOOK_DEBUG then
                Printf("[HOOK] sent page update: %d", pageIndex)
            end
        end

        local function markStateDirty()
            stateDirty = true
        end

        local function markAllExecsDirty()
            for _, execNo in ipairs(executorsToWatch) do
                execDirty[execNo] = true
            end
        end

        local function beginPageGuard()
            pageSettleGuardTicks = PAGE_SETTLE_GUARD_TICKS
            forceReload = true
            markStateDirty()
            markAllExecsDirty()
        end

        local function getCurrentPageIndex(fallback)
            local p = CurrentExecPage()
            if p and p.index ~= nil then
                return p.index
            end
            return fallback or 1
        end

        local function clearHooks()
            if not hooksEnabled then
                return
            end
            for _, id in ipairs(hookIds) do
                pcall(function() Unhook(id) end)
            end
            hookIds = {}
        end

        local function hookObject(handle, onChange, target, label)
            if handle == nil then
                return nil
            end
            local callback = function()
                onChange()
            end

            local ok = false
            local hookId = nil
            local resolvedTarget = handle
            if target ~= nil and type(target) == "userdata" then
                resolvedTarget = target
            end

            ok, hookId = pcall(function()
                return HookObjectChange(callback, handle, pluginHandle, resolvedTarget)
            end)

            if ok and hookId ~= nil and hookId ~= -1 then
                if HOOK_DEBUG then
                    Printf("[HOOK] ok id=%s label=%s", tostring(hookId), tostring(label or "?"))
                end
                return hookId
            end
            if HOOK_DEBUG then
                Printf("[HOOK] failed label=%s handle=%s err=%s", tostring(label or "?"), tostring(handle), tostring(hookId))
            end
            return nil
        end

        local function hookCurrentPage(pageIndex)
            if not hooksEnabled then
                return 0
            end

            clearHooks()
            local registered = 0

            local function onPossiblePageChange(sourceTag)
                local currentPageIndex = getCurrentPageIndex(destPage)
                if currentPageIndex ~= lastPageIndex then
                    destPage = currentPageIndex
                    lastPageIndex = currentPageIndex
                    pageDirty = true
                    sendImmediatePageUpdate(destPage)
                    beginPageGuard()
                    if HOOK_DEBUG then
                        Printf("[HOOK] page change via %s -> %d", tostring(sourceTag), destPage)
                    end
                    return
                end
                markAllExecsDirty()
                markStateDirty()
            end

            local profile = CurrentProfile()
            local hookId = hookObject(profile, function() onPossiblePageChange("profile") end, nil, "profile")
            if hookId ~= nil then
                hookIds[#hookIds + 1] = hookId
                registered = registered + 1
            end

            if HOOK_DEBUG then
                Printf("[HOOK] page %d active (%d hooks, pluginHandle=%s)", pageIndex, registered, tostring(pluginHandle ~= nil))
            end
            markAllExecsDirty()
            return registered
        end

        if hooksEnabled then
            local registered = hookCurrentPage(destPage)
            pageDirty = false
            if registered > 0 then
                Printf("Hook mode active (page change hook only).")
                Printf("Hook plugin handle type: " .. tostring(pluginHandleType))
                Printf("Hook plugin handle source: " .. tostring(pluginLookupSource))
            else
                hooksEnabled = false
                Printf("Hook API found but hook registration failed; using polling fallback.")
                Printf("Hook plugin handle type: " .. tostring(pluginHandleType))
                Printf("Hook plugin handle source: " .. tostring(pluginLookupSource))
            end
        else
            Printf(
                string.format(
                    "Hook mode unavailable (HookObjectChange=%s Unhook=%s pluginHandle=%s args=%d); using polling fallback.",
                    tostring(hasHookObjectChange),
                    tostring(hasUnhook),
                    tostring(hasPluginHandle),
                    argCount
                )
            )
            Printf("Hook plugin handle type: " .. tostring(pluginHandleType))
            Printf("Hook plugin handle source: " .. tostring(pluginLookupSource))
            Printf(
                string.format(
                    "Hook chunk context: plugin=%s component=%s componentHandle=%s",
                    tostring(CHUNK_PLUGIN_NAME),
                    tostring(CHUNK_COMPONENT_NAME),
                    tostring(CHUNK_LUA_COMPONENT_HANDLE ~= nil)
                )
            )
        end

        while (GetVar(GlobalVars(), "EvoFaderWingRunning")) do
            if pageSettleGuardTicks > 0 then
                pageSettleGuardTicks = pageSettleGuardTicks - 1
            end

            if GetVar(GlobalVars(), "forceReload") == true then
                forceReload = true
                markStateDirty()
                markAllExecsDirty()
                SetVar(GlobalVars(), "forceReload", false)
            end

            if autoResendInterval > 0 then
                resendTick = resendTick + 1
                if resendTick >= autoResendInterval then
                    forceReload = true
                    markStateDirty()
                    markAllExecsDirty()
                    resendTick = 0
                    Printf(string.format("Auto force reload triggered (every %.2f seconds)", ticksToSeconds(autoResendInterval)))
                end
            end

            if autoRefreshInterval > 0 then
                refreshTick = refreshTick + 1
                if refreshTick >= autoRefreshInterval then
                    markStateDirty()
                    markAllExecsDirty()
                    refreshTick = 0
                end
            end

            local currentDeskLock = isDeskLocked()
            if currentDeskLock ~= lastDeskLockState then
                lastDeskLockState = currentDeskLock
                deskLockChanged = true
                markStateDirty()
                Printf("Desk lock state changed: " .. (currentDeskLock and "LOCKED" or "UNLOCKED"))
                if not currentDeskLock then
                    forceReload = true
                    markAllExecsDirty()
                    Printf("Desk unlocked - forcing full data resend")
                end
            end

            -- CMD mode: poll MA3 command line state and push changes via wingStatus flags
            local currentCmdFlags = getCmdFlags()
            if currentCmdFlags ~= lastCmdFlags then
                lastCmdFlags = currentCmdFlags
                cmdFlagsChanged = true
                Printf("CMD mode changed: flags=" .. currentCmdFlags)
            end

            local myPage = CurrentExecPage()
            local currentPageIndex = destPage
            if myPage and myPage.index ~= nil then
                currentPageIndex = myPage.index
            end
            if currentPageIndex ~= lastPageIndex then
                destPage = currentPageIndex
                lastPageIndex = currentPageIndex
                pageDirty = true
                beginPageGuard()
                sendImmediatePageUpdate(destPage)
            end

            if hooksEnabled and pageDirty then
                hookCurrentPage(destPage)
                pageDirty = false
            end

            markStateDirty()
            if not hooksEnabled then
                markAllExecsDirty()
            end

            if stateDirty or forceReload or deskLockChanged or cmdFlagsChanged then
                local refreshDetails = forceReload or pageDirty or not hooksEnabled
                if not refreshDetails then
                    for _, execNo in ipairs(executorsToWatch) do
                        if execDirty[execNo] then
                            refreshDetails = true
                            break
                        end
                    end
                end

                local state = collectExecState(refreshDetails, execDirty, destPage)

                local confirmPage = CurrentExecPage()
                if confirmPage and confirmPage.index ~= nil and confirmPage.index ~= destPage then
                    destPage = confirmPage.index
                    lastPageIndex = destPage
                    pageDirty = true
                    beginPageGuard()
                    sendImmediatePageUpdate(destPage)
                    if hooksEnabled then
                        hookCurrentPage(destPage)
                        pageDirty = false
                    end
                    state = collectExecState(true, execDirty, destPage)
                end

                local allowDeltaSend = pageSettleGuardTicks <= 0

                -- Send wingStatus FIRST when desk lock changes so the wing can reset
                -- fader ownership before the execUpdate setpoints arrive.
                -- Format: /wingStatus,ii,<deskLock 0|1>,<cmdFlags>
                if forceReload or deskLockChanged or cmdFlagsChanged then
                    local deskLockInt = lastDeskLockState and 1 or 0
                    sendOsc("/wingStatus,ii," .. deskLockInt .. "," .. lastCmdFlags)
                    Printf("Sent wing status: deskLock=" .. deskLockInt .. " cmdFlags=" .. lastCmdFlags)
                    deskLockChanged = false
                    cmdFlagsChanged = false
                end

                if forceReload or (allowDeltaSend and (state.faderDataChanged or state.statusChanged)) then
                    local execMessage = "/execUpdate" .. execUpdateTypeTag .. "," .. destPage

                    for i = 201, 210 do
                        local faderValue = state.currentFaderValues[i] or 0
                        execMessage = execMessage .. "," .. math.floor(faderValue)
                        oldValues[i] = faderValue
                    end

                    for _, execNo in ipairs(executorsToWatch) do
                        local statusCode = state.currentExecStatus[execNo] or 0
                        execMessage = execMessage .. "," .. statusCode
                        oldExecStatus[execNo] = state.currentExecStatus[execNo]
                    end

                    sendOsc(execMessage)
                    Printf("Sent exec update: Page " .. destPage .. ".")
                end

                if forceReload or (allowDeltaSend and state.colorChanged) then
                    local colorMessage = "/colorUpdate" .. colorUpdateTypeTag .. "," .. destPage
                    for _, execNo in ipairs(executorsToWatch) do
                        local c = state.currentColorValues[execNo] or "0,0,0,0"
                        local cSemi = string.gsub(c, ",", ";")
                        colorMessage = colorMessage .. "," .. cSemi
                        oldColorValues[execNo] = c
                    end

                    sendOsc(colorMessage)
                    Printf("Sent color update: Page " .. destPage .. ".")
                end

                forceReload = false
                stateDirty = false
            end

            coroutine.yield(tick)
        end

        clearHooks()

    elseif name == "Stop" then
        Printf("Stopping EvoFaderWing0.4 OSC...")
        SetVar(GlobalVars(), "EvoFaderWingRunning", false)

    elseif name == "Force Reload" then
        Printf("Force reload triggered...")
        SetVar(GlobalVars(), "forceReload", true)

    else
        Printf("Canceled")
    end
end

return main
