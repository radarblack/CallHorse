local this = {}

this.name = "WhistleHorse"
this.native = nil
this.nativeStatus = "not loaded yet"
this.presetCommand = nil
this.learnedCommand = nil
this.useSpy = true
this.spyActive = false
this.maxRetryFrames = 300

local DLL_NAME = "CallHorse.dll"
local ENTRY_POINT = "luaopen_CallHorse"
local LIBRARY_NAME = "CallHorseNative"

local wasDown = false
local lastState = nil
local lastSequence = 0
local lastBuddySequence = 0
local learning = false
local loadAttempted = false
local statusShown = false
local pending = nil
local loggedResult = nil

local function logQuiet(message)
  InfCore.Log("WhistleHorse: " .. message, false, true)
end

local function log(message)
  logQuiet(message)
  TppUiCommand.AnnounceLogView("Whistle: " .. message)
end

local function horseType()
  return (BuddyType and BuddyType.HORSE) or 1
end

local function describe(state)
  return string.format("%d,%d,%d,%d", state[1], state[2], state[3], state[4])
end

local function describeCommand(command)
  return string.format(
    "kind=%d type=%d bytes=%d,%d,%d,%d flag=%d",
    command.kind, command.type, command.a18, command.a1c, command.a20, command.a24, command.f64
  )
end

local function sameState(a, b)
  return a ~= nil and b ~= nil and a[1] == b[1] and a[2] == b[2] and a[3] == b[3] and a[4] == b[4]
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
        elseif library.RunBuddyCommand == nil or library.GetLastGameBuddyCommand == nil then
          status = "DLL IS OLD: UPDATE CallHorse.dll"
          logQuiet(status .. " (" .. path .. ")")
        else
          this.native = library
          this.nativeStatus = "ready"
          logQuiet("native library ready from " .. path)
          this.StartSpy()
          return true
        end
      end
    end
  end

  this.nativeStatus = status
  return false
end

function this.StartSpy()
  this.spyActive = false

  if not this.useSpy or this.native.EnableSpy == nil then
    logQuiet("spy not used")
    return
  end

  local ok, enabled = pcall(this.native.EnableSpy, true)
  if ok and enabled == true then
    this.spyActive = true
    logQuiet("spy active: game buddy commands are being captured")
  else
    logQuiet("spy could not start (" .. tostring(enabled) .. ")")
  end
end

function this.PollBuddyCommand()
  local ok, sequence, kind, buddyType, x, y, z, rotation, a18, a1c, a20, a24, f64, result =
    pcall(this.native.GetLastGameBuddyCommand)

  if not ok or type(sequence) ~= "number" then
    return nil
  end

  return sequence, {
    kind = kind,
    type = buddyType,
    x = x,
    y = y,
    z = z,
    rotation = rotation,
    a18 = a18,
    a1c = a1c,
    a20 = a20,
    a24 = a24,
    f64 = f64,
    result = result,
  }
end

function this.TrackBuddyCommand()
  local sequence, command = this.PollBuddyCommand()
  if sequence == nil or sequence == lastBuddySequence then
    return
  end

  lastBuddySequence = sequence

  logQuiet(string.format(
    "game buddy command #%d %s pos=(%.1f,%.1f,%.1f) rot=%.1f result=%d | player=(%.1f,%.1f,%.1f) rotY=%.1f",
    sequence,
    describeCommand(command),
    command.x, command.y, command.z, command.rotation, command.result,
    vars.playerPosX, vars.playerPosY, vars.playerPosZ, vars.playerRotY
  ))

  if command.type ~= horseType() then
    return
  end

  if learning then
    learning = false
    this.learnedCommand = command
    log("WHISTLE LEARNED " .. describeCommand(command) .. " - PRESS LIGHT_SWITCH TO USE IT")
  else
    log("GAME HORSE COMMAND " .. describeCommand(command))
  end
end

function this.TrackGameCommand()
  local ok, sequence, e61, e62, e63, e64 = pcall(this.native.GetLastGameHorseCommand)
  if not ok or type(sequence) ~= "number" or sequence == lastSequence then
    return
  end

  lastSequence = sequence
  logQuiet("game horse state command #" .. sequence .. " " .. describe({ e61, e62, e63, e64 }))
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
end

function this.StepRequest()
  if pending == nil then
    return
  end

  local command = pending.command
  pending.frames = pending.frames + 1

  local ok, result = pcall(
    this.native.RunBuddyCommand,
    command.kind,
    command.type,
    vars.playerPosX,
    vars.playerPosY,
    vars.playerPosZ,
    vars.playerRotY,
    command.a18,
    command.a1c,
    command.a20,
    command.a24,
    command.f64
  )

  if not ok then
    log("COMMAND ERROR: " .. tostring(result))
    pending = nil
    return
  end

  if result == false then
    log("COMMAND REFUSED (buddy manager not ready or invalid command)")
    pending = nil
    return
  end

  if result ~= loggedResult then
    loggedResult = result
    logQuiet("run result " .. tostring(result) .. " at frame " .. pending.frames)
  end

  if result == 0 then
    log("WHISTLE DONE AFTER " .. pending.frames .. " FRAMES")
    pending = nil
    return
  end

  if pending.frames >= this.maxRetryFrames then
    log("WHISTLE GAVE UP, LAST RESULT " .. tostring(result))
    pending = nil
  end
end

function this.StartRequest(command)
  pending = { command = command, frames = 0 }
  loggedResult = nil
  this.StepRequest()
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
    if not this.spyActive then
      log("CANNOT LEARN WITHOUT THE SPY - SET this.presetCommand")
      return false
    end

    learning = true
    local sequence = this.PollBuddyCommand()
    if sequence ~= nil then
      lastBuddySequence = sequence
    end
    log("LEARNING: HOLD Q, SELECT WHISTLE ONCE IN THE RADIAL MENU")
    return false
  end

  this.StartRequest(command)
  return true
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
    if this.spyActive then
      local okBuddy, buddyError = pcall(this.TrackBuddyCommand)
      if not okBuddy then
        logQuiet("TrackBuddyCommand error: " .. tostring(buddyError))
      end

      local okGame, gameError = pcall(this.TrackGameCommand)
      if not okGame then
        logQuiet("TrackGameCommand error: " .. tostring(gameError))
      end
    end

    local okState, stateError = pcall(this.TrackState)
    if not okState then
      logQuiet("TrackState error: " .. tostring(stateError))
    end

    local okStep, stepError = pcall(this.StepRequest)
    if not okStep then
      log("STEP ERROR: " .. tostring(stepError))
      pending = nil
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
