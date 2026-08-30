local socket = require("socket")
local server = assert(socket.bind("0.0.0.0", 8080))
print("Serving HTTP on http://0.0.0.0:8080 ...")

while true do
  local client = server:accept()
  local line = client:receive() -- Read HTTP request line
  
  local body = "<html><body><h1>Hello from dmaOS HTTP Server!</h1></body></html>\r\n"
  local response = "HTTP/1.1 200 OK\r\n" ..
                   "Content-Type: text/html\r\n" ..
                   "Content-Length: " .. string.len(body) .. "\r\n" ..
                   "Connection: close\r\n\r\n" .. body

  client:send(response)
  client:close()
end
