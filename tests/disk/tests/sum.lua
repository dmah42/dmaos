-- Counts the lines and bytes read from stdin and writes "<lines> <bytes>
-- <last line>" to the file arg[1].
local lines, bytes, last = 0, 0, ""
for l in io.lines() do
  lines = lines + 1
  bytes = bytes + #l + 1
  last = l
end
local f = assert(io.open(arg[1], "w"))
f:write(lines, " ", bytes, " ", last, "\n")
f:close()
