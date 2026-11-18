-- Tremolo: an example MXJ plugin (MIT licence).
-- MXJ AUDIO calls process() for each block of the selection, one after the other.

local phase = 0

function prepare(sampleRate, channels, params)
  phase = 0
end

function process(block, params)
  local step = 2 * math.pi * params.rate / block.sampleRate
  local depth = params.depth / 100
  local hard = params.shape == 1

  for i = 1, block.frames do
    local wave = math.sin(phase)
    if hard then
      wave = wave >= 0 and 1 or -1
    end

    local gain = 1 - depth * (0.5 + 0.5 * wave)
    for c = 1, block.channels do
      block:set(c, i, block:get(c, i) * gain)
    end

    phase = phase + step
    if phase > 2 * math.pi then
      phase = phase - 2 * math.pi
    end
  end
end
