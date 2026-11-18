-- Bit Crusher: an example MXJ plugin (MIT licence).

local held = {}
local counter = 0

function prepare(sampleRate, channels, params)
  held = {}
  counter = 0
end

function process(block, params)
  local levels = 2 ^ (math.floor(params.bits) - 1)
  local hold = math.max(1, math.floor(params.downsample))
  local mix = params.mix / 100

  for i = 1, block.frames do
    local take = counter % hold == 0
    counter = counter + 1

    for c = 1, block.channels do
      local x = block:get(c, i)
      if take or held[c] == nil then
        held[c] = math.floor(x * levels + 0.5) / levels
      end
      block:set(c, i, x * (1 - mix) + held[c] * mix)
    end
  end
end
