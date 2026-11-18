-- Silence Finder: an example analysis plugin (MIT licence).
-- analyse() reads each block; finish() returns the report MXJ AUDIO shows.

local found = {}
local quietSince = nil
local rate = 48000

local function clock(frame)
  local seconds = frame / rate
  return string.format("%d:%06.3f", math.floor(seconds / 60), seconds % 60)
end

local function close(endFrame, params)
  if quietSince ~= nil and (endFrame - quietSince) / rate >= params.minimum then
    table.insert(found, { quietSince, endFrame })
  end
  quietSince = nil
end

function prepare(sampleRate, channels, params)
  found = {}
  quietSince = nil
  rate = sampleRate
end

function analyse(block, params)
  local threshold = mxj.dbToGain(params.threshold)

  for i = 1, block.frames do
    local peak = 0
    for c = 1, block.channels do
      local x = math.abs(block:get(c, i))
      if x > peak then peak = x end
    end

    local frame = block.position + i - 1
    if peak < threshold then
      if quietSince == nil then quietSince = frame end
    else
      close(frame, params)
    end
  end

  if block.position + block.frames >= block.length then
    close(block.length, params)
  end
end

function finish(params)
  if #found == 0 then
    return "No silence found (quieter than " .. params.threshold .. " dB for at least " .. params.minimum .. " s)."
  end

  local lines = { #found .. " quiet parts:" }
  for _, part in ipairs(found) do
    table.insert(lines, clock(part[1]) .. "  (" .. string.format("%.2f", (part[2] - part[1]) / rate) .. " s)")
  end
  return table.concat(lines, "\n")
end
