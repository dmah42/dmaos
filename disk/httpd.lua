local socket = require("socket")

local server = assert(socket.bind("0.0.0.0", 8080))
print("[httpd] Serving HTTP on http://0.0.0.0:8080 ...")

while true do
  print("[httpd] Awaiting client connection...")
  local client, err = server:accept()
  if not client then
    print("[httpd] accept error: " .. tostring(err))
  else
    local request_line, rerr = client:receive()
    if request_line then
      print("[httpd] Request: " .. request_line)
      -- Consume incoming HTTP headers until empty line (\r\n\r\n)
      while true do
        local header = client:receive()
        if not header or header == "" then
          break
        end
      end

      local body = "<html><body><h1>Hello from dmaOS HTTP Server!</h1><p>Served by Lua 5.4</p></body></html>\r\n"
      local response = "HTTP/1.1 200 OK\r\n" ..
                       "Content-Type: text/html\r\n" ..
                       "Content-Length: " .. tostring(string.len(body)) .. "\r\n" ..
                       "Connection: close\r\n\r\n" .. body

      local sent, serr = client:send(response)
      if sent then
        print("[httpd] Response: 200 OK (" .. sent .. " bytes)")
      else
        print("[httpd] send error: " .. tostring(serr))
      end
    else
      print("[httpd] receive error: " .. tostring(rerr))
    end
    client:close()
  end
end
