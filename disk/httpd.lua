local socket = require("socket")

-- Usage: lua /httpd.lua [root_dir] [port]
local root = arg[1] or "/www"
local port = tonumber(arg[2]) or 8080
root = root:gsub("/+$", "")

local CHUNK_SIZE = 4096

local mime_types = {
  html = "text/html",
  htm = "text/html",
  css = "text/css",
  js = "text/javascript",
  json = "application/json",
  txt = "text/plain",
  lua = "text/plain",
  cfg = "text/plain",
  png = "image/png",
  jpg = "image/jpeg",
  jpeg = "image/jpeg",
  gif = "image/gif",
  svg = "image/svg+xml",
  ico = "image/x-icon",
}

local function mime_type(path)
  local ext = path:match("%.([%w]+)$")
  return (ext and mime_types[ext:lower()]) or "application/octet-stream"
end

local function url_decode(s)
  return (s:gsub("%%(%x%x)", function(hex)
    return string.char(tonumber(hex, 16))
  end))
end

-- Map a request target to a filesystem path, or nil if it is not allowed
local function resolve(target)
  local path = url_decode(target:gsub("[?#].*$", ""))
  if path:sub(1, 1) ~= "/" then
    return nil
  end
  for segment in path:gmatch("[^/]+") do
    if segment == ".." then
      return nil
    end
  end
  return path
end

local function file_exists(path)
  local f = io.open(path, "rb")
  if f then
    f:close()
    return true
  end
  return false
end

local function send_response(client, status, headers, body)
  local lines = { "HTTP/1.1 " .. status }
  for name, value in pairs(headers) do
    lines[#lines + 1] = name .. ": " .. value
  end
  lines[#lines + 1] = "Connection: close"
  local head = table.concat(lines, "\r\n") .. "\r\n\r\n"
  return client:send(head .. (body or ""))
end

local function send_error(client, status)
  local body = "<html><body><h1>" .. status .. "</h1></body></html>\r\n"
  local sent, err = send_response(client, status, {
    ["Content-Type"] = "text/html",
    ["Content-Length"] = tostring(#body),
  }, body)
  return status, sent, err
end

local function send_file(client, method, file_path)
  local f = io.open(file_path, "rb")
  if not f then
    return send_error(client, "404 Not Found")
  end
  local size = f:seek("end")
  f:seek("set", 0)

  local status = "200 OK"
  local sent, err = send_response(client, status, {
    ["Content-Type"] = mime_type(file_path),
    ["Content-Length"] = tostring(size),
  })
  if sent and method == "GET" then
    while true do
      local chunk = f:read(CHUNK_SIZE)
      if not chunk then
        break
      end
      local n
      n, err = client:send(chunk)
      if not n then
        sent = nil
        break
      end
      sent = sent + n
    end
  end
  f:close()
  return status, sent, err
end

local function handle(client, request_line)
  local method, target = request_line:match("^(%u+)%s+(%S+)%s+HTTP/%d%.%d$")
  if not method then
    return send_error(client, "400 Bad Request")
  end
  if method ~= "GET" and method ~= "HEAD" then
    return send_error(client, "405 Method Not Allowed")
  end

  local path = resolve(target)
  if not path then
    return send_error(client, "400 Bad Request")
  end

  if path:sub(-1) == "/" then
    return send_file(client, method, root .. path .. "index.html")
  end
  -- A directory requested without a trailing slash: redirect so relative links resolve
  if file_exists(root .. path .. "/index.html") then
    local status = "301 Moved Permanently"
    local sent, err = send_response(client, status, {
      ["Location"] = path .. "/",
      ["Content-Length"] = "0",
    })
    return status, sent, err
  end
  return send_file(client, method, root .. path)
end

local server = assert(socket.bind("0.0.0.0", port))
print("[httpd] Serving " .. root .. " on http://0.0.0.0:" .. port .. " ...")

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

      local status, sent, serr = handle(client, request_line)
      if sent then
        print("[httpd] Response: " .. status .. " (" .. sent .. " bytes)")
      else
        print("[httpd] send error: " .. tostring(serr))
      end
    else
      print("[httpd] receive error: " .. tostring(rerr))
    end
    client:close()
  end
end
