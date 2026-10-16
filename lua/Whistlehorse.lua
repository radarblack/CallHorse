local this = {}

this.name = "WhistleHorse"
this.native = nil
this.nativeStatus = "not loaded yet"
this.presetCommand = nil
this.learnedCommand = nil

local DLL_NAME = "CallHorse.dll"
local ENTRY_POINT = "luaopen_CallHorse"
local LIBRARY_NAME = "CallHorseNative"

local wasDown = false
local lastState = nil
local learning = false
local loadAttempted = false
local statusShown = false

local function logQuiet(message)
  InfCore.Log("WhistleHorse: " .. message, false, true)
end

local function log(message)
  logQuiet(message)
  TppUiCommand.AnnounceLogView("Whistle: " .. message)
end

local function describe(state)
  return string.format("%d,%d,%d,%d", state[1], state[2], state[3], state[4])
end

local function sameState(a, b)
  return a ~= nil and b ~= nil and a[1] == b[1] and a[2] == b[2] and a[3] == b[3] and a[4] == b[4]
end

local function isZero(state)
  return state[1] == 0 and state[2] == 0 and state[3] == 0 and state[4] == 0
end

local function candidatePaths()
  local paths = {}
  if InfCore ~= nil and InfCore.gamePath ~= nil then
    paths[#paths + 1] = InfCore.gamePath .. "plugins\\" .. DLL_NAME
  end
  paths[#paths + 1] = "plugins\\" .. DLL_NAME

  for index, path in ipairs(paths) do
    paths[index] = string.gsub(path, "/", "\\")
  end

  return paths
end

function this.LoadNative()
  if this.native ~= nil then
    return true
  end

  local status = "no plugin path to try"

  for _, path in ipairs(candidatePaths()) do
    logQuiet("loading " .. path)
    local entry, loadError = package.loadlib(path, ENTRY_POINT)

    if entry == nil then
      status = "DLL NOT LOADED: " .. tostring(loadError)
      logQuiet(status .. " (" .. path .. ")")
    else
      local ok, library = pcall(entry)

      if not ok then
        status = "ENTRY POINT ERROR: " .. tostring(library)
        logQuiet(status .. " (" .. path .. ")")
      else
        if type(library) ~= "table" then
          library = rawget(_G, LIBRARY_NAME)
        end

        if type(library) ~= "table" or library.IsAvailable == nil then
          status = "DLL LOADED BUT NO LIBRARY, SEE CallHorse.log"
          logQuiet(status .. " (" .. path .. ")")
        elseif library.IsAvailable() ~= true then
          status = "DLL LOADED BUT GAME BUILD UNSUPPORTED, SEE CallHorse.log"
          logQuiet(status .. " (" .. path .. ")")
        elseif library.GetHorseCommandState == nil or library.SetHorseCommandState == nil then
          status = "DLL IS OLD: UPDATE CallHorse.dll"
          logQuiet(status .. " (" .. path .. ")")
        else
          this.native = library
          this.nativeStatus = "ready"
          logQuiet("native library ready from " .. path)
          return true
        end
      end
    end
  end

  this.nativeStatus = status
  return false
end

function this.ReadState()
  if this.native == nil then
    return nil
  end

  local ok, e61, e62, e63, e64 = pcall(this.native.GetHorseCommandState)
  if not ok or type(e61) ~= "number" then
    return nil
  end

  return { e61, e62, e63, e64 }
end

function this.ExecuteCommand(command)
  local okReset, reset = pcall(this.native.SetHorseCommandState, 0, 0, 0, 0, false)
  if not okReset or reset ~= true then
    log("RESET FAILED: " .. tostring(reset))
    return false
  end

  local ok, accepted = pcall(
    this.native.SetHorseCommandState,
    command[1],
    command[2],
    command[3],
    command[4],
    true
  )

  if not ok then
    log("COMMAND ERROR: " .. tostring(accepted))
    return false
  end

  if accepted ~= true then
    log("COMMAND REFUSED (player or buddy manager not ready)")
    return false
  end

  log("WHISTLE SENT " .. describe(command))
  return true
end

function this.TrackState()
  local state = this.ReadState()
  if state == nil then
    return
  end

  if lastState == nil then
    lastState = state
    logQuiet("horse command state " .. describe(state))
    return
  end

  if sameState(state, lastState) then
    return
  end

  logQuiet("horse command state " .. describe(lastState) .. " -> " .. describe(state))
  lastState = state

  if learning and not isZero(state) then
    learning = false
    this.learnedCommand = state
    log("WHISTLE LEARNED " .. describe(state) .. " - PRESS LIGHT_SWITCH TO USE IT")
  end
end

function this.OnPress()
  if not this.LoadNative() then
    if not statusShown then
      statusShown = true
      log("NATIVE UNAVAILABLE - " .. this.nativeStatus)
    end
    return false
  end

  local command = this.presetCommand or this.learnedCommand

  if command == nil then
    learning = true
    lastState = this.ReadState()
    log("LEARNING: HOLD Q, SELECT WHISTLE ONCE IN THE RADIAL MENU")
    return false
  end

  return this.ExecuteCommand(command)
end

function this.Update()
  local buttons = PlayerVars.scannedButtonsDirect
  if buttons == nil then
    return
  end

  if this.native == nil and not loadAttempted then
    loadAttempted = true
    this.LoadNative()
  end

  if this.native ~= nil then
    local ok, trackError = pcall(this.TrackState)
    if not ok then
      logQuiet("TrackState error: " .. tostring(trackError))
    end
  end

  local down = bit.band(buttons, PlayerPad.LIGHT_SWITCH) ~= 0
  if down and not wasDown then
    local ok, pressError = pcall(this.OnPress)
    if not ok then
      log("ERROR: " .. tostring(pressError))
    end
  end
  wasDown = down
end

return this
