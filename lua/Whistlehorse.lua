local this = {}

this.name = "WhistleHorse"
this.native = nil

local DLL_NAME = "CallHorse.dll"
local ENTRY_POINT = "luaopen_CallHorse"
local LIBRARY_NAME = "CallHorseNative"

local wasDown = false

local function log(message)
  InfCore.Log("WhistleHorse: " .. message)
  TppUiCommand.AnnounceLogView("Whistle: " .. message)
end

local function candidatePaths()
  local paths = {}
  if InfCore ~= nil and InfCore.gamePath ~= nil then
    paths[#paths + 1] = InfCore.gamePath .. "plugins/" .. DLL_NAME
  end
  paths[#paths + 1] = "plugins/" .. DLL_NAME
  return paths
end

function this.LoadNative()
  if this.native ~= nil then
    return true
  end

  for _, path in ipairs(candidatePaths()) do
    local entry, loadError = package.loadlib(path, ENTRY_POINT)
    if entry ~= nil then
      local ok, library = pcall(entry)
      if not ok then
        InfCore.Log("WhistleHorse: entry point failed for " .. path .. ": " .. tostring(library))
      else
        if type(library) ~= "table" then
          library = rawget(_G, LIBRARY_NAME)
        end

        if type(library) == "table" and library.IsAvailable ~= nil and library.IsAvailable() then
          this.native = library
          InfCore.Log("WhistleHorse: native library ready from " .. path)
          return true
        end

        InfCore.Log("WhistleHorse: native library unavailable (unsupported game build?) from " .. path)
      end
    else
      InfCore.Log("WhistleHorse: could not load " .. path .. ": " .. tostring(loadError))
    end
  end

  return false
end

function this.WhistleNative(buddyType, rotationRadians)
  local ok, accepted = pcall(
    this.native.Whistle,
    buddyType,
    vars.playerPosX,
    vars.playerPosY,
    vars.playerPosZ,
    rotationRadians,
    true
  )

  if not ok then
    log("NATIVE ERROR: " .. tostring(accepted))
    return false
  end

  if accepted then
    log("NATIVE CALL ACCEPTED")
  else
    log("NATIVE CALL REJECTED (no buddy, call disabled, or manager missing)")
  end

  return accepted == true
end

function this.WhistleLua(buddyType, rotationRadians)
  local controller = TppBuddy2BlockController
  if controller == nil then
    log("CONTROLLER MISSING")
    return false
  end

  local position = Vector3(vars.playerPosX, vars.playerPosY, vars.playerPosZ)

  local okPosition, positionError = pcall(controller.SetCallPosition, position, rotationRadians)
  if not okPosition then
    log("SET POSITION FAILED: " .. tostring(positionError))
    return false
  end

  local okCall, accepted = pcall(controller.CallBuddy, buddyType, position, rotationRadians)
  if not okCall then
    log("CALL ERROR: " .. tostring(accepted))
    return false
  end

  if accepted then
    log("LUA CALL ACCEPTED (mode 0, may not move the horse)")
  else
    log("LUA CALL REJECTED (no buddy, call in progress, or call disabled)")
  end

  return accepted == true
end

function this.Whistle()
  local buddyType = BuddyType and BuddyType.HORSE
  if buddyType == nil then
    log("BUDDYTYPE.HORSE MISSING")
    return false
  end

  local rotationRadians = math.rad(vars.playerRotY)

  if this.LoadNative() then
    return this.WhistleNative(buddyType, rotationRadians)
  end

  return this.WhistleLua(buddyType, rotationRadians)
end

function this.Update()
  local buttons = PlayerVars.scannedButtonsDirect
  if buttons == nil then
    return
  end

  local down = bit.band(buttons, PlayerPad.LIGHT_SWITCH) ~= 0
  if down and not wasDown then
    this.Whistle()
  end
  wasDown = down
end

return this
