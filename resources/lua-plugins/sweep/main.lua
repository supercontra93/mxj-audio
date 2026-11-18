-- Sine Sweep: an example generator (MIT licence).
-- generate() fills each block; block.position tells where the block starts.

local phase = 0

function prepare(sampleRate, channels, params)
  phase = 0
end

function generate(block, params)
  local total = math.max(2, params.duration * block.sampleRate)
  local gain = mxj.dbToGain(params.level)

  for i = 1, block.frames do
    local t = math.min(1, (block.position + i - 1) / (total - 1))
    local f
    if params.curve == 1 then
      f = params.from * (params.to / params.from) ^ t
    else
      f = params.from + (params.to - params.from) * t
    end

    phase = phase + 2 * math.pi * f / block.sampleRate
    local value = math.sin(phase) * gain
    for c = 1, block.channels do
      block:set(c, i, value)
    end
  end

  phase = phase % (2 * math.pi)
end
