-- EVOFaderWing Lua script for syncing EVOFaderWing using OSC
-- Sends ALL data as ONE OSC message: page + fader values (0-100) + color values
-- OSC sent when data has changed or after autoResendInterval
--
-- Set autoResendInterval via: SetVar(GlobalVars(), "autoResendInterval", 600) 
-- (600 = 30 seconds, since main loop runs every 0.05 seconds)
-- Default is 300 (15 seconds)
--
-- This program is free software: you can redistribute it and/or modify
-- it under the terms of the GNU General Public License as published by
-- the Free Software Foundation, either version 3 of the License, or
-- (at your option) any later version.
-- This program is distributed in the hope that it will be useful,
-- but WITHOUT ANY WARRANTY; without even the implied warranty of
-- MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
-- GNU General Public License for more details.
-- You should have received a copy of the GNU General Public License
-- along with this program.  If not, see <https://www.gnu.org/licenses/>. 



local function StartGui()
    -- Check current status
    local isRunning = GetVar(GlobalVars(), "opdateOSC") == true
    
    local descTable = {
        title = "EvoFaderWing OSC Control",
        caller = GetFocusDisplay(),
        add_args = {FilterSupport = "Yes"},
    }
    
    if isRunning then
        descTable.items = {"Stop", "Force Reload"}
        descTable.message = "EvoFaderWing OSC is RUNNING"
    else
        descTable.items = {"Start", "Force Reload"}  
        descTable.message = "EvoFaderWing OSC is STOPPED"
    end

    local index, name = PopupInput(descTable)
    return index, name
end

function main()
    local index, name = StartGui()

    if name == "Start" then
        Printf("Starting EvoFaderWing OSC...")
        -- Simply set the variable to true and let the original script run
        SetVar(GlobalVars(), "opdateOSC", true)
        
        
        local executorsToWatch = {}
        local oldValues = {}
        local oldColorValues = {}

        local oscEntry = 2

        -- Configure executors to watch: faders 201-210 AND 101-110 for color support when using 101-110 as well
        for i = 201, 210 do
            executorsToWatch[#executorsToWatch + 1] = i
        end
        for i = 101, 110 do
            executorsToWatch[#executorsToWatch + 1] = i
        end

        -- Set the default values for faders 201-210
        for i = 201, 210 do
            oldValues[i] = "000"
        end

        -- Set the default color values for both ranges (201-210 primary, 101-110 secondary)
        for i = 201, 210 do
            oldColorValues[i] = "0,0,0,0"
        end
        for i = 101, 110 do
            oldColorValues[i] = "0,0,0,0"
        end

        -- The speed to check executors
        local tick = 1 / 20 -- 1/20 second = 50ms
        local resendTick = 0

        local function getAppearanceColor(sequence)
            local apper = sequence["APPEARANCE"]
            if apper ~= nil then
                return apper['BACKR'] .. "," .. apper['BACKG'] .. "," .. apper['BACKB'] .. "," .. apper['BACKALPHA']
            else
                return "255,255,255,255"
            end
        end

        -- Get automatic resend interval (in 20ths of seconds) - default to 300 (15 seconds)
        local autoResendInterval = GetVar(GlobalVars(), "autoResendInterval") or 300

        Printf("start EvoFaderWing OSC - watching faders 201-210 (values) + 201-210,101-110 Colors")
        Printf("autoResendInterval: " .. autoResendInterval .. " (every " .. (autoResendInterval / 20) .. " seconds)")

        local destPage = 1
        local forceReload = true

        while (GetVar(GlobalVars(), "opdateOSC")) do
            -- Get current auto resend interval (can be changed at runtime)
            local autoResendInterval = GetVar(GlobalVars(), "autoResendInterval") or 300
            
            if GetVar(GlobalVars(), "forceReload") == true then
                forceReload = true
                SetVar(GlobalVars(), "forceReload", false)
            end

            -- Increment resend counter and check for automatic resend 
            resendTick = resendTick + 1
            if resendTick >= autoResendInterval then
                forceReload = true
                resendTick = 0
                Printf("Auto force reload triggered (every " .. (autoResendInterval / 20) .. " seconds)")
            end

            local dataChanged = false

            -- Check Page
            local myPage = CurrentExecPage()
            if myPage.index ~= destPage then
                destPage = myPage.index
                -- Reset fader values (201-210)
                for i = 201, 210 do
                    oldValues[i] = 000
                end
                -- Reset color values for both ranges (201-210 primary, 101-110 secondary)
                for i = 201, 210 do
                    oldColorValues[i] = "0,0,0,0"
                end
                for i = 101, 110 do
                    oldColorValues[i] = "0,0,0,0"
                end
                forceReload = true
                dataChanged = true
            end

            -- Get all Executors
            local executors = DataPool().Pages[destPage]:Children()

            -- Collect all current values
            local currentFaderValues = {}
            local currentColorValues = {}

            -- Collect data from all watched executors (201-210 and 101-110)
            for listKey, listValue in pairs(executorsToWatch) do
                local faderValue = 0
                local colorValue = "0,0,0,0"

                -- Set Fader & Color Values
                for maKey, maValue in pairs(executors) do
                    if maValue.No == listValue then
                        -- Only get fader values from 201-210 range
                        if listValue >= 201 and listValue <= 210 then
                            local faderOptions = {}
                            faderOptions.value = faderEnd
                            faderOptions.token = "FaderMaster"
                            faderOptions.faderDisabled = false

                            faderValue = maValue:GetFader(faderOptions)
                            currentFaderValues[listValue] = faderValue
                        end

                        -- Get color values from both ranges (201-210 and 101-110)
                        local myobject = maValue.Object
                        if myobject ~= nil then
                            colorValue = getAppearanceColor(myobject)
                        end
                        currentColorValues[listValue] = colorValue
                    end
                end

                -- Check if fader values changed (201-210 only)
                if listValue >= 201 and listValue <= 210 then
                    if oldValues[listValue] ~= faderValue then
                        dataChanged = true
                    end
                end

                -- Check if color values changed (both ranges)
                if oldColorValues[listValue] ~= colorValue then
                    dataChanged = true
                end
            end

            -- Send single OSC message with ALL data if anything changed
            if dataChanged or forceReload then
                -- Build OSC message: page + 10 fader values (0-100) + 10 dual color strings
                local oscMessage = "/faderUpdate,iiiiiiiiiiissssssssss," .. destPage
                
                -- Add all fader values (201-210) as 0-100 integers (no conversion needed)
                for i = 201, 210 do
                    local faderValue = currentFaderValues[i] or 0
                    oscMessage = oscMessage .. "," .. math.floor(faderValue)
                    oldValues[i] = faderValue
                end
                
                -- Add all dual color values (primary from 201-210, secondary from 101-110)
                for i = 1, 10 do
                    local primaryFader = 200 + i  -- 201-210
                    local secondaryFader = 100 + i  -- 101-110
                    
                    local primaryColor = currentColorValues[primaryFader] or "0,0,0,0"
                    local secondaryColor = currentColorValues[secondaryFader] or "0,0,0,0"
                    
                    -- Convert comma separators to semicolons and combine primary + secondary
                    local primaryColorSemicolon = string.gsub(primaryColor, ",", ";")
                    local secondaryColorSemicolon = string.gsub(secondaryColor, ",", ";")
                    local combinedColor = primaryColorSemicolon .. ";" .. secondaryColorSemicolon
                    
                    oscMessage = oscMessage .. "," .. combinedColor
                    
                    -- Update old values for both ranges
                    oldColorValues[primaryFader] = primaryColor
                    oldColorValues[secondaryFader] = secondaryColor
                end

                -- Send the single packet containing everything
                Cmd('SendOSC ' .. oscEntry .. ' "' .. oscMessage .. '"')
                Printf("Sent fader update: Page " .. destPage .. ".")
            end
            
            forceReload = false

            -- Main loop delay
            coroutine.yield(tick)
        end
        
    elseif name == "Stop" then
        Printf("Stopping EvoFaderWing OSC...")
        SetVar(GlobalVars(), "opdateOSC", false)
        
    elseif name == "Force Reload" then
        Printf("Force reload triggered...")
        SetVar(GlobalVars(), "forceReload", true)
        
    else
        Printf("Canceled")
    end
end

return main