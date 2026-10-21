local this = {}

this.name = "WhistleHorse"
this.native = nil
this.nativeStatus = "not loaded yet"
this.presetCommand = nil
this.learnedChain = nil
this.learnOnPress = true
this.stayBackCommand = { kind = 1, type = 1, a18 = 0, a1c = 0, a20 = 0, a24 = 0, f64 = 0 }
this.useSpy = true
this.spyActive = false
this.usePlayerPose = false
this.maxRetryFrames = 300
this.captureQuietFrames = 600

local DLL_NAME = "CallHorse.dll"
local ENTRY_POINT = "luaopen_CallHorse"
local LIBRARY_NAME = "CallHorseNative"
local RING_WINDOW = 60

local wasDown = false
local lastState = nil
local lastSequence = 0
local lastBuddySequence = 0
local frame = 0
local capture = nil
local loadAttempted = false
local statusShown = false
local pending = nil
local loggedResults = {}

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

local function describeCommand(command)
  return string.format(
    "kind=%d type=%d bytes=%d,%d,%d,%d flag=%d",
    command.kind, command.type, command.a18, command.a1c, command.a20, command.a24, command.f64
  )
end

local function copyCommand(command)
  return {
    kind = command.kind,
    type = command.type,
    x = command.x,
    y = command.y,
    z = command.z,
    rotation = command.rotation,
    a18 = command.a18,
    a1c = command.a1c,
    a20 = command.a20,
    a24 = command.a24,
    f64 = command.f64,
  }
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
        elseif library.RunBuddyCommand == nil or library.GetGameBuddyCommand == nil then
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

local function unpackCommand(ok, sequence, kind, buddyType, x, y, z, rotation, a18, a1c, a20, a24, f64, result)
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

function this.PollLatestSequence()
  local sequence = unpackCommand(pcall(this.native.GetLastGameBuddyCommand))
  return sequence
end

function this.PollCommand(sequence)
  return unpackCommand(pcall(this.native.GetGameBuddyCommand, sequence))
end

function this.OnBuddyCommand(sequence, command)
  logQuiet(string.format(
    "game buddy command #%d %s pos=(%.1f,%.1f,%.1f) rot=%.1f result=%d | player=(%.1f,%.1f,%.1f) rotY=%.1f",
    sequence,
    describeCommand(command),
    command.x, command.y, command.z, command.rotation, command.result,
    vars.playerPosX, vars.playerPosY, vars.playerPosZ, vars.playerRotY
  ))

  if capture ~= nil then
    capture.items[#capture.items + 1] = { command = copyCommand(command), sequence = sequence, frame = frame }
    capture.lastFrame = frame
  end
end

function this.TrackBuddyCommand()
  local latest = this.PollLatestSequence()
  if latest == nil or latest == lastBuddySequence then
    return
  end

  local first = lastBuddySequence + 1
  if latest - first >= RING_WINDOW then
    first = latest - RING_WINDOW + 1
  end

  for sequence = first, latest do
    local found, command = this.PollCommand(sequence)
    if found ~= nil then
      this.OnBuddyCommand(sequence, command)
    end
  end

  lastBuddySequence = latest
end

function this.FinishCapture()
  if capture == nil or #capture.items == 0 then
    return false
  end

  local items = capture.items
  capture = nil

  local startFrame = items[1].frame
  local chain = {}
  local parts = {}

  for index, item in ipairs(items) do
    local _, latest = this.PollCommand(item.sequence)
    local finalResult = latest ~= nil and latest.result or -1

    chain[index] = {
      command = item.command,
      offset = item.frame - startFrame,
      retry = finalResult == 0,
      waitPrevious = false,
    }

    parts[index] = string.format("+%df %s final=%d", item.frame - startFrame, describeCommand(item.command), finalResult)
  end

  this.learnedChain = chain
  logQuiet("learned chain: " .. table.concat(parts, " | "))
  log("WHISTLE LEARNED " .. #chain .. " COMMAND(S) OVER " .. (items[#items].frame - startFrame) .. " FRAMES - PRESS LIGHT_SWITCH TO USE IT")
  return true
end

function this.CheckCapture()
  if capture == nil or #capture.items == 0 then
    return
  end

  if frame - capture.lastFrame >= this.captureQuietFrames then
    this.FinishCapture()
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

function this.RunOne(command)
  local x, y, z, rotation = command.x, command.y, command.z, command.rotation

  if this.usePlayerPose or x == nil or y == nil or z == nil or rotation == nil then
    x, y, z, rotation = vars.playerPosX, vars.playerPosY, vars.playerPosZ, vars.playerRotY
  end

  return pcall(
    this.native.RunBuddyCommand,
    command.kind,
    command.type,
    x,
    y,
    z,
    rotation,
    command.a18,
    command.a1c,
    command.a20,
    command.a24,
    command.f64
  )
end

local function allDone(request)
  for index = 1, #request.entries do
    if request.state[index] ~= "done" then
      return false
    end
  end

  return true
end

local function runEntry(index, entry)
  pending.ran[index] = frame
  pending.total = pending.total + 1

  local ok, result = this.RunOne(entry.command)

  if not ok then
    log("COMMAND ERROR: " .. tostring(result))
    pending = nil
    return false
  end

  if result == false then
    log("COMMAND REFUSED (buddy manager not ready or invalid command)")
    pending = nil
    return false
  end

  if loggedResults[index] ~= result then
    loggedResults[index] = result
    logQuiet(string.format("run %d/%d %s result %s at step %d", index, #pending.entries, describeCommand(entry.command), tostring(result), pending.total))
  end

  if result == 0 then
    pending.state[index] = "done"
    return true
  end

  if not entry.retry then
    pending.state[index] = "done"
    return true
  end

  pending.frames[index] = pending.frames[index] + 1

  if pending.frames[index] >= this.maxRetryFrames then
    log("WHISTLE GAVE UP ON COMMAND " .. index .. ", LAST RESULT " .. tostring(result))
    pending = nil
    return false
  end

  return false
end

function this.StepRequest()
  if pending == nil then
    return
  end

  local progress = true
  local passes = 0

  while progress and pending ~= nil and passes <= #pending.entries do
    progress = false
    passes = passes + 1

    local elapsed = frame - pending.startFrame

    for index, entry in ipairs(pending.entries) do
      if pending == nil then
        return
      end

      if pending.state[index] == nil then
        local previousDone = index == 1 or pending.state[index - 1] == "done"
        if elapsed >= entry.offset and (not entry.waitPrevious or previousDone) then
          pending.state[index] = "active"
          pending.frames[index] = 0
        end
      end

      if pending.state[index] == "active" and pending.ran[index] ~= frame then
        if runEntry(index, entry) then
          progress = true
        end
      end
    end

    if pending ~= nil and allDone(pending) then
      log("WHISTLE DONE AFTER " .. pending.total .. " STEPS")
      pending = nil
      return
    end
  end
end

function this.StartRequest(entries)
  pending = {
    entries = entries,
    startFrame = frame,
    state = {},
    frames = {},
    ran = {},
    total = 0,
  }
  loggedResults = {}
  this.StepRequest()
end

local function presetEntries()
  local preset = this.presetCommand
  if preset == nil then
    return nil
  end

  local list = preset
  if preset.kind ~= nil then
    list = { preset }
  end

  local entries = {}
  for index, command in ipairs(list) do
    entries[index] = { command = command, offset = 0, retry = true, waitPrevious = true }
  end

  return entries
end

function this.OnPress()
  if not this.LoadNative() then
    if not statusShown then
      statusShown = true
      log("NATIVE UNAVAILABLE - " .. this.nativeStatus)
    end
    return false
  end

  if capture ~= nil then
    if #capture.items > 0 then
      this.FinishCapture()
    else
      log("STILL LEARNING: SELECT WHISTLE IN THE RADIAL MENU FIRST")
    end
    return false
  end

  local entries = presetEntries() or this.learnedChain

  if entries == nil then
    if not this.learnOnPress or not this.spyActive then
      log("NO WHISTLE COMMAND: SET this.presetCommand")
      return false
    end

    local latest = this.PollLatestSequence()
    if latest ~= nil then
      lastBuddySequence = latest
    end

    capture = { items = {}, lastFrame = frame }
    log("LEARNING: HOLD Q, SELECT WHISTLE ONCE, THEN PRESS LIGHT_SWITCH AGAIN")
    return false
  end

  this.StartRequest(entries)
  return true
end

function this.StayBack()
  if not this.LoadNative() then
    return false
  end

  this.StartRequest({ { command = this.stayBackCommand, offset = 0, retry = true, waitPrevious = true } })
  return true
end

function this.Update()
  local buttons = PlayerVars.scannedButtonsDirect
  if buttons == nil then
    return
  end

  frame = frame + 1

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

      local okCapture, captureError = pcall(this.CheckCapture)
      if not okCapture then
        logQuiet("CheckCapture error: " .. tostring(captureError))
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
