local this = {}

this.name = "WhistleHorse"

local wasDown = false

local function log(message)
  InfCore.Log("WhistleHorse: " .. message)
  TppUiCommand.AnnounceLogView("Whistle: " .. message)
end

function this.PlayerPose()
  local position = Vector3(vars.playerPosX, vars.playerPosY, vars.playerPosZ)
  return position, math.rad(vars.playerRotY)
end

function this.NativeAvailable()
  if HookSample == nil or HookSample.IsBuddyWhistleAvailable == nil then
    return false
  end

  local ok, available = pcall(HookSample.IsBuddyWhistleAvailable)
  return ok and available == true
end

function this.WhistleNative(buddyType, rotationRadians)
  local ok, accepted = pcall(
    HookSample.WhistleBuddy,
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

function this.WhistleLua(buddyType, position, rotationRadians)
  local controller = TppBuddy2BlockController
  if controller == nil then
    log("CONTROLLER MISSING")
    return false
  end

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

  local position, rotationRadians = this.PlayerPose()

  if this.NativeAvailable() then
    return this.WhistleNative(buddyType, rotationRadians)
  end

  return this.WhistleLua(buddyType, position, rotationRadians)
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
