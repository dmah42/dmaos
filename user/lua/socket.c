#include "socket.h"

#include <arpa/inet.h>
#include <errno.h>
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "k_time.h"
#include "user.h"

// Metatable Identifiers
#define SOCKET_SERVER_MT "dmaos.socket.server"
#define SOCKET_CLIENT_MT "dmaos.socket.client"

// Lua CFunction Return Counts (number of values pushed onto Lua evaluation
// stack)
#define LUA_RET_VOID 0
#define LUA_RET_SINGLE 1
#define LUA_RET_PAIR 2

#define SOCKET_BUF_SIZE 1024

typedef struct {
  int fd;
  bool closed;
  char buf[SOCKET_BUF_SIZE];
  size_t buf_pos;
  size_t buf_len;
} l_socket_t;

// Push (nil, error_message) onto the Lua stack and return 2 stack values
static int l_socket_error(lua_State *L, const char *err_msg) {
  printf("[lua-socket] error: %s (errno=%d)\n", err_msg, errno);
  lua_pushnil(L);
  lua_pushstring(L, err_msg);
  return LUA_RET_PAIR;
}

// Push (nil, "closed") onto the Lua stack and return 2 stack values
static int l_socket_closed(lua_State *L) {
  lua_pushnil(L);
  lua_pushliteral(L, "closed");
  return LUA_RET_PAIR;
}

static l_socket_t *create_socket_ud(lua_State *L, int fd, const char *mt) {
  l_socket_t *s = (l_socket_t *)lua_newuserdata(L, sizeof(l_socket_t));
  s->fd = fd;
  s->closed = false;
  s->buf_pos = 0;
  s->buf_len = 0;
  luaL_getmetatable(L, mt);
  lua_setmetatable(L, -2);
  return s;
}

static l_socket_t *check_socket(lua_State *L, int index, const char *mt) {
  l_socket_t *s = (l_socket_t *)luaL_checkudata(L, index, mt);
  if (s->closed || s->fd < 0) {
    luaL_error(L, "attempt to use a closed socket");
  }
  return s;
}

static ssize_t socket_fill_buffer(l_socket_t *s, uint32_t timeout_ms) {
  if (s->buf_pos < s->buf_len) {
    return (ssize_t)(s->buf_len - s->buf_pos);
  }

  s->buf_pos = 0;
  s->buf_len = 0;

  uint64_t start_ticks = uptime();
  uint64_t timeout_ticks = (uint64_t)timeout_ms * (uint64_t)TICKS_PER_MS;

  while (!s->closed) {
    ssize_t n = read(s->fd, s->buf, sizeof(s->buf));
    if (n > 0) {
      s->buf_len = (size_t)n;
      return n;
    }
    if (n == 0) {
      return 0; // EOF / Connection closed
    }
    if (errno != ENOENT && errno != EAGAIN && errno != EWOULDBLOCK) {
      printf("[lua-socket] fill_buffer: read(fd=%d) error (errno=%d: %s)\n",
             s->fd, errno, strerror(errno));
      return -1; // Genuine error
    }
    if (timeout_ms > 0 && (uptime() - start_ticks) >= timeout_ticks) {
      errno = ETIMEDOUT;
      return -1;
    }
    yield();
  }
  return -1;
}

// -----------------------------------------------------------------------------
// Server Socket Methods
// -----------------------------------------------------------------------------

static int l_server_accept(lua_State *L) {
  l_socket_t *srv = check_socket(L, 1, SOCKET_SERVER_MT);

  struct sockaddr_in cli_addr;
  socklen_t cli_len = sizeof(cli_addr);

  // Block until a client connects or socket is closed
  while (!srv->closed) {
    int client_fd = accept(srv->fd, (struct sockaddr *)&cli_addr, &cli_len);
    if (client_fd >= 0) {
      create_socket_ud(L, client_fd, SOCKET_CLIENT_MT);
      return LUA_RET_SINGLE;
    }
    if (errno != ENOENT && errno != EAGAIN && errno != EWOULDBLOCK) {
      return l_socket_error(L, strerror(errno));
    }
    yield();
  }

  return l_socket_error(L, "server socket closed");
}

static int l_server_close(lua_State *L) {
  l_socket_t *srv = (l_socket_t *)luaL_checkudata(L, 1, SOCKET_SERVER_MT);
  if (!srv->closed && srv->fd >= 0) {
    close(srv->fd);
    srv->closed = true;
    srv->fd = -1;
  }
  lua_pushboolean(L, 1);
  return LUA_RET_SINGLE;
}

static int l_server_tostring(lua_State *L) {
  l_socket_t *srv = (l_socket_t *)luaL_checkudata(L, 1, SOCKET_SERVER_MT);
  if (srv->closed) {
    lua_pushstring(L, "socket (server, closed)");
  } else {
    lua_pushfstring(L, "socket (server, fd: %d)", srv->fd);
  }
  return LUA_RET_SINGLE;
}

// -----------------------------------------------------------------------------
// Client Socket Methods
// -----------------------------------------------------------------------------

static int l_client_receive(lua_State *L) {
  l_socket_t *cli = check_socket(L, 1, SOCKET_CLIENT_MT);

  // Determine mode:
  // - default or "*l": read a single line (stripping trailing \r and \n)
  // - number n: read up to n bytes
  // - "*a": read all data until closed
  const char *pattern = "*l";
  lua_Integer num_bytes = -1;

  if (lua_isinteger(L, 2)) {
    num_bytes = lua_tointeger(L, 2);
  } else if (lua_isstring(L, 2)) {
    pattern = lua_tostring(L, 2);
  }

  // Read fixed number of bytes
  if (num_bytes >= 0) {
    if (num_bytes == 0) {
      lua_pushliteral(L, "");
      return LUA_RET_SINGLE;
    }
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    size_t remaining = (size_t)num_bytes;
    while (remaining > 0) {
      ssize_t avail = socket_fill_buffer(cli, 10000);
      if (avail < 0) {
        return l_socket_error(L, strerror(errno));
      }
      if (avail == 0) {
        if (remaining == (size_t)num_bytes) {
          return l_socket_closed(L);
        }
        break;
      }
      size_t take = (size_t)avail;
      if (take > remaining)
        take = remaining;
      luaL_addlstring(&b, cli->buf + cli->buf_pos, take);
      cli->buf_pos += take;
      remaining -= take;
    }
    luaL_pushresult(&b);
    return LUA_RET_SINGLE;
  }

  // Read all until EOF
  if (strcmp(pattern, "*a") == 0) {
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    while (true) {
      ssize_t avail = socket_fill_buffer(cli, 2000);
      if (avail < 0) {
        if (errno == ETIMEDOUT)
          break;
        return l_socket_error(L, strerror(errno));
      }
      if (avail == 0)
        break;
      luaL_addlstring(&b, cli->buf + cli->buf_pos, (size_t)avail);
      cli->buf_pos += (size_t)avail;
    }
    luaL_pushresult(&b);
    return LUA_RET_SINGLE;
  }

  // Default: Read line until '\n' (stripping '\r' and '\n')
  luaL_Buffer b;
  luaL_buffinit(L, &b);
  bool got_newline = false;
  bool got_any = false;

  while (!got_newline) {
    ssize_t avail = socket_fill_buffer(cli, 10000);
    if (avail < 0) {
      if (!got_any) {
        return l_socket_error(L, strerror(errno));
      }
      break;
    }
    if (avail == 0) {
      if (!got_any) {
        return l_socket_closed(L);
      }
      break;
    }

    while (cli->buf_pos < cli->buf_len) {
      char c = cli->buf[cli->buf_pos++];
      got_any = true;
      if (c == '\n') {
        got_newline = true;
        break;
      }
      if (c != '\r') {
        luaL_addchar(&b, c);
      }
    }
  }

  luaL_pushresult(&b);
  return LUA_RET_SINGLE;
}

static int l_client_send(lua_State *L) {
  l_socket_t *cli = check_socket(L, 1, SOCKET_CLIENT_MT);
  size_t len = 0;
  const char *data = luaL_checklstring(L, 2, &len);

  lua_Integer start = luaL_optinteger(L, 3, 1);
  if (start < 1)
    start = 1;
  if ((size_t)start > len + 1) {
    lua_pushinteger(L, 0);
    return LUA_RET_SINGLE;
  }

  size_t offset = (size_t)(start - 1);
  size_t to_send = len - offset;
  size_t total_sent = 0;

  while (total_sent < to_send && !cli->closed) {
    ssize_t sent =
        write(cli->fd, data + offset + total_sent, to_send - total_sent);
    if (sent > 0) {
      total_sent += (size_t)sent;
      continue;
    }
    if (sent < 0) {
      if (errno == ENOENT || errno == EAGAIN || errno == EWOULDBLOCK) {
        yield();
        continue;
      }
      printf("[lua-socket] send(fd=%d): write error %d (%s)\n", cli->fd, errno,
             strerror(errno));
      return l_socket_error(L, strerror(errno));
    }
    if (sent == 0) {
      break;
    }
  }

  lua_pushinteger(L, (lua_Integer)(offset + total_sent));
  return LUA_RET_SINGLE;
}

static int l_client_close(lua_State *L) {
  l_socket_t *cli = (l_socket_t *)luaL_checkudata(L, 1, SOCKET_CLIENT_MT);
  if (!cli->closed && cli->fd >= 0) {
    close(cli->fd);
    cli->closed = true;
    cli->fd = -1;
  }
  lua_pushboolean(L, 1);
  return LUA_RET_SINGLE;
}

static int l_client_tostring(lua_State *L) {
  l_socket_t *cli = (l_socket_t *)luaL_checkudata(L, 1, SOCKET_CLIENT_MT);
  if (cli->closed) {
    lua_pushstring(L, "socket (client, closed)");
  } else {
    lua_pushfstring(L, "socket (client, fd: %d)", cli->fd);
  }
  return LUA_RET_SINGLE;
}

// -----------------------------------------------------------------------------
// Module Functions
// -----------------------------------------------------------------------------

static int l_socket_bind(lua_State *L) {
  const char *host = luaL_optstring(L, 1, "0.0.0.0");
  int port = (int)luaL_checkinteger(L, 2);

  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return l_socket_error(L, strerror(errno));
  }

  struct sockaddr_in serv_addr;
  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons((uint16_t)port);
  serv_addr.sin_addr.s_addr = inet_addr(host);

  if (bind(fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
    close(fd);
    return l_socket_error(L, strerror(errno));
  }

  if (listen(fd, 8) < 0) {
    close(fd);
    return l_socket_error(L, strerror(errno));
  }

  create_socket_ud(L, fd, SOCKET_SERVER_MT);
  return LUA_RET_SINGLE;
}

static int l_socket_connect(lua_State *L) {
  const char *host = luaL_checkstring(L, 1);
  int port = (int)luaL_checkinteger(L, 2);

  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return l_socket_error(L, strerror(errno));
  }

  struct sockaddr_in serv_addr;
  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons((uint16_t)port);
  serv_addr.sin_addr.s_addr = inet_addr(host);

  if (connect(fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
    close(fd);
    return l_socket_error(L, strerror(errno));
  }

  create_socket_ud(L, fd, SOCKET_CLIENT_MT);
  return LUA_RET_SINGLE;
}

static int l_socket_gettime(lua_State *L) {
  lua_pushnumber(L, (lua_Number)uptime() / (lua_Number)TICKS_PER_SEC);
  return LUA_RET_SINGLE;
}

// -----------------------------------------------------------------------------
// Metatables & Module Registration
// -----------------------------------------------------------------------------

static const struct luaL_Reg server_methods[] = {
    {"accept", l_server_accept},
    {"close", l_server_close},
    {NULL, NULL},
};

static const struct luaL_Reg client_methods[] = {
    {"receive", l_client_receive},
    {"send", l_client_send},
    {"close", l_client_close},
    {NULL, NULL},
};

static const struct luaL_Reg socket_funcs[] = {
    {"bind", l_socket_bind},
    {"connect", l_socket_connect},
    {"gettime", l_socket_gettime},
    {NULL, NULL},
};

static void register_metatable(lua_State *L, const char *mt_name,
                               const struct luaL_Reg *methods,
                               lua_CFunction tostring_fn, lua_CFunction gc_fn) {
  luaL_newmetatable(L, mt_name);
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");

  luaL_setfuncs(L, methods, 0);

  if (tostring_fn) {
    lua_pushcfunction(L, tostring_fn);
    lua_setfield(L, -2, "__tostring");
  }
  if (gc_fn) {
    lua_pushcfunction(L, gc_fn);
    lua_setfield(L, -2, "__gc");
  }

  lua_pop(L, 1);
}

int luaopen_socket(lua_State *L) {
  register_metatable(L, SOCKET_SERVER_MT, server_methods, l_server_tostring,
                     l_server_close);
  register_metatable(L, SOCKET_CLIENT_MT, client_methods, l_client_tostring,
                     l_client_close);

  luaL_newlib(L, socket_funcs);
  return LUA_RET_SINGLE;
}
