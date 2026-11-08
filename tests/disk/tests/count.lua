-- Counts 1..5 about a second apart, busy-waiting in between, and writes each
-- count with its time to the file arg[1].
local f = assert(io.open(arg[1], "w"))
for i = 1, 5 do
  f:write(i, " ", os.time(), "\n")
  local t = os.time() + 1
  while os.time() < t do end
end
f:close()
