-- Spins for arg[1] seconds without making a syscall, then writes the time it
-- finished to the file arg[2]. Without preemption, nothing else runs meanwhile.
local secs, out = tonumber(arg[1]), arg[2]
local t = os.time() + secs
while os.time() < t do end
local f = assert(io.open(out, "w"))
f:write(os.time(), "\n")
f:close()
