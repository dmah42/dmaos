-- Writes 2000 numbered lines (22000 bytes) to stdout, far more than a pipe's
-- 512-byte buffer, so the writer has to block and be woken by the reader.
for i = 1, 2000 do
  io.write(string.format("line %5d\n", i))
end
