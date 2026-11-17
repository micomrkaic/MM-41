-- pandoc filter: number the headings of the plain-text manual and mark them
-- so that `manual <chapter>` can find them with a regular expression.
local counters = {0, 0, 0}

function Header(h)
  local lvl = h.level
  if lvl > 3 then return h end
  counters[lvl] = counters[lvl] + 1
  for i = lvl + 1, 3 do counters[i] = 0 end
  local num = tostring(counters[1])
  if lvl >= 2 then num = num .. "." .. counters[2] end
  if lvl == 3 then num = num .. "." .. counters[3] end
  local title = pandoc.utils.stringify(h.content)
  local line
  if lvl == 1 then
    line = string.rep("=", 88) .. "\n" .. num .. "  " .. string.upper(title) .. "\n" .. string.rep("=", 88)
  elseif lvl == 2 then
    line = num .. "  " .. title .. "\n" .. string.rep("-", #num + 2 + #title)
  else
    line = num .. "  " .. title
  end
  return pandoc.RawBlock("plain", line)
end
