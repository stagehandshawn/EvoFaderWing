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
        local DEBUG_PROXY = false
        local DEBUG_PROXY_LINK = false

        local execMetaCache = {}
        local execMetaPageIndex = -1

        local function getAppearanceColor(target)
            local apper = target and target["APPEARANCE"] or nil
            if apper ~= nil then
                return apper["BACKR"] .. "," .. apper["BACKG"] .. "," .. apper["BACKB"] .. "," .. apper["BACKALPHA"]
            end
            return "255,255,255,255"
        end

        local function extractAppearance(target)
            if target == nil then return nil end
            local ok, ap = pcall(function() return target["APPEARANCE"] end)
            if ok and ap ~= nil then return ap end
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
                return 0, "0,0,0,0", 0, nil, false
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
                local ap = extractAppearance(meta.sequenceObject)
                    or extractAppearance(meta.sequenceHostObject)
                    or extractAppearance(meta.primaryObject)
                    or extractAppearance(meta.handle)
                if ap ~= nil then
                    colorValue = ap["BACKR"] .. "," .. ap["BACKG"] .. "," .. ap["BACKB"] .. "," .. ap["BACKALPHA"]
                else
                    colorValue = getAppearanceColor(meta.primaryObject or meta.handle)
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
                local wantsColor = true
                local refreshThis = refreshDetails or (execDirtyFlags and execDirtyFlags[execNo] == true)
                local wantsColorThis = wantsColor and refreshThis

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

                if wantsColor then
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

            if stateDirty or forceReload then
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
