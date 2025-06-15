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

local executorsToWatch = {}
local oldValues = {}
local oldColorValues = {}

local oscEntry = 2

-- Configure executors to watch: only faders 201-210
for i = 201, 210 do
    executorsToWatch[#executorsToWatch + 1] = i
end

-- Set the default values
for _, number in ipairs(executorsToWatch) do
    oldValues[number] = "000"
    oldColorValues[number] = "0,0,0,0"
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

local function main()
    -- Get automatic resend interval (in 20ths of seconds) - default to 300 (15 seconds)
    local autoResendInterval = GetVar(GlobalVars(), "autoResendInterval") or 300

    Printf("start pam OSC main() - watching faders 201-210 (single packet, 0-100 scale)")
    Printf("autoResendInterval: " .. autoResendInterval .. " (every " .. (autoResendInterval / 20) .. " seconds)")

    local destPage = 1
    local forceReload = true

    if GetVar(GlobalVars(), "opdateOSC") ~= nil then
        SetVar(GlobalVars(), "opdateOSC", not GetVar(GlobalVars(), "opdateOSC"))
    else
        SetVar(GlobalVars(), "opdateOSC", true)
    end

    while (GetVar(GlobalVars(), "opdateOSC")) do
        -- Get current auto resend interval (can be changed at runtime)
        local autoResendInterval = GetVar(GlobalVars(), "autoResendInterval") or 300
        
        if GetVar(GlobalVars(), "forceReload") == true then
            forceReload = true
            SetVar(GlobalVars(), "forceReload", false)
        end

        -- Increment resend counter and check for automatic resend   UN COMMET FOR AUTO RESEND
        -- resendTick = resendTick + 1
        -- if resendTick >= autoResendInterval then
        --     forceReload = true
        --     resendTick = 0
        --     Printf("Auto force reload triggered (every " .. (autoResendInterval / 20) .. " seconds)")
        -- end

        local dataChanged = false

        -- Check Page
        local myPage = CurrentExecPage()
        if myPage.index ~= destPage then
            destPage = myPage.index
            for maKey, maValue in pairs(oldValues) do
                oldValues[maKey] = 000
            end
            for maKey, maValue in pairs(oldColorValues) do
                oldColorValues[maKey] = "0,0,0,0"
            end
            forceReload = true
            dataChanged = true
        end

        -- Get all Executors
        local executors = DataPool().Pages[destPage]:Children()

        -- Collect all current values
        local currentFaderValues = {}
        local currentColorValues = {}

        for listKey, listValue in pairs(executorsToWatch) do
            local faderValue = 0
            local colorValue = "0,0,0,0"

            -- Set Fader & Color Values
            for maKey, maValue in pairs(executors) do
                if maValue.No == listValue then
                    local faderOptions = {}
                    faderOptions.value = faderEnd
                    faderOptions.token = "FaderMaster"
                    faderOptions.faderDisabled = false

                    faderValue = maValue:GetFader(faderOptions)

                    local myobject = maValue.Object
                    if myobject ~= nil then
                        colorValue = getAppearanceColor(myobject)
                    end
                end
            end

            currentFaderValues[listValue] = faderValue
            currentColorValues[listValue] = colorValue

            -- Check if anything changed
            if oldValues[listValue] ~= faderValue or oldColorValues[listValue] ~= colorValue then
                dataChanged = true
            end
        end

        -- Send single OSC message with ALL data if anything changed
        if dataChanged or forceReload then
            -- Build OSC message: page + 10 fader values (0-100) + 10 color strings
            -- Format: /faderUpdate,iiiiiiiiiiissssssssss,PAGE,F201,F202...F210,C201,C202...C210
            local oscMessage = "/faderUpdate,iiiiiiiiiiissssssssss," .. destPage
            
            -- Add all fader values (201-210) as 0-100 integers (no conversion needed)
            for i = 201, 210 do
                local faderValue = currentFaderValues[i] or 0
                oscMessage = oscMessage .. "," .. math.floor(faderValue)
                oldValues[i] = faderValue
            end
            
            -- Add all color values (201-210)
            for i = 201, 210 do
                local colorValue = currentColorValues[i] or "0,0,0,0"
                local newValue = string.gsub(colorValue, ",", ";")
                oscMessage = oscMessage .. "," .. newValue
                oldColorValues[i] = colorValue
            end

            -- Send the single packet containing everything
            Cmd('SendOSC ' .. oscEntry .. ' "' .. oscMessage .. '"')
            Printf("Sent fader update: Page " .. destPage .. " with all fader (0-100) and color data")
        end
        
        forceReload = false

        -- Main loop delay
        coroutine.yield(tick)
    end
end

return main