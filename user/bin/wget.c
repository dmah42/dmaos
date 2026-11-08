#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#include "colors.h"
#include "k_time.h"

#define MAX_REDIRECTS 5
#define HEADER_BUF_SIZE 4096
#define BODY_BUF_SIZE 1024
#define READ_TIMEOUT_MS 10000
// Sockets are non-blocking; wait this long before retrying. Each retry also
// polls the network, so a short interval keeps downloads fast.
#define SOCKET_RETRY_MS 1

struct url {
  char host[128];
  uint16_t port;
  char path[512];
};

static void usage(void) {
  fprintf(stderr, "usage: wget [-O <file>] http://host[:port]/path\n"
                  "  -O <file>  save to <file> ('-' for stdout)\n");
}

static bool parse_url(const char *s, struct url *u) {
  const char *prefix = "http://";
  if (strncmp(s, "https://", 8) == 0) {
    fprintf(stderr, BOLD RED "wget: https is not supported\n" DEFAULT);
    return false;
  }
  if (strncmp(s, prefix, strlen(prefix)) == 0) {
    s += strlen(prefix);
  }

  size_t host_len = strcspn(s, ":/");
  if (host_len == 0 || host_len >= sizeof(u->host)) {
    fprintf(stderr, BOLD RED "wget: invalid host in URL\n" DEFAULT);
    return false;
  }
  memcpy(u->host, s, host_len);
  u->host[host_len] = '\0';
  s += host_len;

  u->port = 80;
  if (*s == ':') {
    char *end;
    long port = strtol(s + 1, &end, 10);
    if (end == s + 1 || port <= 0 || port > 65535) {
      fprintf(stderr, BOLD RED "wget: invalid port in URL\n" DEFAULT);
      return false;
    }
    u->port = (uint16_t)port;
    s = end;
  }

  const char *path = (*s == '\0') ? "/" : s;
  if (*path != '/' || strlen(path) >= sizeof(u->path)) {
    fprintf(stderr, BOLD RED "wget: invalid path in URL\n" DEFAULT);
    return false;
  }
  strcpy(u->path, path);
  return true;
}

// Default output name: last path segment, or index.html for a directory
static void default_filename(const struct url *u, char *out, size_t len) {
  char path[sizeof(u->path)];
  strcpy(path, u->path);
  path[strcspn(path, "?#")] = '\0';

  const char *name = strrchr(path, '/');
  name = name ? name + 1 : path;
  snprintf(out, len, "%s", (*name == '\0') ? "index.html" : name);
}

static int connect_to(const struct url *u) {
  struct hostent *he = gethostbyname(u->host);
  if (!he) {
    fprintf(stderr, BOLD RED "wget: failed to resolve '%s'\n" DEFAULT, u->host);
    return -1;
  }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(u->port);
  memcpy(&addr.sin_addr, he->h_addr_list[0], sizeof(addr.sin_addr));

  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    fprintf(stderr, BOLD RED "wget: socket: %s\n" DEFAULT, strerror(errno));
    return -1;
  }
  if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    fprintf(stderr, BOLD RED "wget: failed to connect to %s:%u: %s\n" DEFAULT,
            inet_ntoa(addr.sin_addr), u->port, strerror(errno));
    close(fd);
    return -1;
  }
  return fd;
}

static bool send_all(int fd, const char *buf, size_t len) {
  while (len > 0) {
    ssize_t n = write(fd, buf, len);
    if (n > 0) {
      buf += n;
      len -= (size_t)n;
      continue;
    }
    if (n < 0 && (errno == ENOENT || errno == EAGAIN)) {
      sleep_ms(SOCKET_RETRY_MS);
      continue;
    }
    return false;
  }
  return true;
}

// Socket reads return ENOENT while no data has arrived yet; wait for data,
// EOF (0) or an error/timeout (-1)
static ssize_t recv_some(int fd, char *buf, size_t len) {
  uint64_t start = uptime();
  while (true) {
    ssize_t n = read(fd, buf, len);
    if (n >= 0) {
      return n;
    }
    if (errno != ENOENT && errno != EAGAIN) {
      return -1;
    }
    if (uptime() - start >= READ_TIMEOUT_MS * TICKS_PER_MS) {
      errno = ETIMEDOUT;
      return -1;
    }
    sleep_ms(SOCKET_RETRY_MS);
  }
}

// Find a header's value (case-insensitive name) within the header block
static bool find_header(const char *headers, const char *name, char *out,
                        size_t len) {
  size_t name_len = strlen(name);
  for (const char *line = headers; line && *line;) {
    if (strncasecmp(line, name, name_len) == 0 && line[name_len] == ':') {
      const char *v = line + name_len + 1;
      while (*v == ' ') {
        ++v;
      }
      size_t vlen = strcspn(v, "\r\n");
      if (vlen >= len) {
        vlen = len - 1;
      }
      memcpy(out, v, vlen);
      out[vlen] = '\0';
      return true;
    }
    line = strchr(line, '\n');
    if (line) {
      ++line;
    }
  }
  return false;
}

int main(int argc, char **argv) {
  const char *out_name = NULL;
  const char *url_arg = NULL;

  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "-O") == 0 && i + 1 < argc) {
      out_name = argv[++i];
    } else if (argv[i][0] == '-' || url_arg) {
      usage();
      return 1;
    } else {
      url_arg = argv[i];
    }
  }
  if (!url_arg) {
    usage();
    return 1;
  }

  struct url u;
  if (!parse_url(url_arg, &u)) {
    return 1;
  }

  static char hdr[HEADER_BUF_SIZE];
  size_t hdr_len = 0;
  char *body_start = NULL;
  int status = 0;
  int fd = -1;

  for (int redirects = 0;; ++redirects) {
    fprintf(stderr, "Connecting to %s:%u...\n", u.host, u.port);
    fd = connect_to(&u);
    if (fd < 0) {
      return 1;
    }

    // HTTP/1.0 keeps the response un-chunked and closed when done
    char req[768];
    int req_len = snprintf(req, sizeof(req),
                           "GET %s HTTP/1.0\r\n"
                           "Host: %s:%u\r\n"
                           "User-Agent: dmaOS-wget\r\n"
                           "Connection: close\r\n\r\n",
                           u.path, u.host, u.port);
    if (!send_all(fd, req, (size_t)req_len)) {
      fprintf(stderr, BOLD RED "wget: failed to send request: %s\n" DEFAULT,
              strerror(errno));
      close(fd);
      return 1;
    }

    // Read until the end of the response headers
    hdr_len = 0;
    body_start = NULL;
    while (!body_start) {
      if (hdr_len >= sizeof(hdr) - 1) {
        fprintf(stderr, BOLD RED "wget: response headers too large\n" DEFAULT);
        close(fd);
        return 1;
      }
      ssize_t n = recv_some(fd, hdr + hdr_len, sizeof(hdr) - 1 - hdr_len);
      if (n <= 0) {
        fprintf(stderr, BOLD RED "wget: no response from server%s%s\n" DEFAULT,
                n < 0 ? ": " : "", n < 0 ? strerror(errno) : "");
        close(fd);
        return 1;
      }
      hdr_len += (size_t)n;
      hdr[hdr_len] = '\0';
      char *end = strstr(hdr, "\r\n\r\n");
      if (end) {
        *end = '\0';
        body_start = end + 4;
      }
    }

    if (sscanf(hdr, "HTTP/%*d.%*d %d", &status) != 1) {
      fprintf(stderr, BOLD RED "wget: malformed response\n" DEFAULT);
      close(fd);
      return 1;
    }
    fprintf(stderr, "HTTP response: %.*s\n", (int)strcspn(hdr + 9, "\r\n"),
            hdr + 9);

    char location[sizeof(u.path)];
    bool is_redirect = status == 301 || status == 302 || status == 303 ||
                       status == 307 || status == 308;
    if (!is_redirect ||
        !find_header(hdr, "Location", location, sizeof(location))) {
      break;
    }
    close(fd);
    if (redirects + 1 >= MAX_REDIRECTS) {
      fprintf(stderr, BOLD RED "wget: too many redirects\n" DEFAULT);
      return 1;
    }
    if (location[0] == '/') {
      strcpy(u.path, location);
    } else if (!parse_url(location, &u)) {
      return 1;
    }
    fprintf(stderr, "Redirected to %s\n", location);
  }

  if (status < 200 || status >= 300) {
    fprintf(stderr,
            BOLD RED "wget: server returned %d, nothing saved\n" DEFAULT,
            status);
    close(fd);
    return 1;
  }

  long content_length = -1;
  char value[32];
  if (find_header(hdr, "Content-Length", value, sizeof(value))) {
    content_length = strtol(value, NULL, 10);
  }

  char name_buf[128];
  if (!out_name) {
    default_filename(&u, name_buf, sizeof(name_buf));
    out_name = name_buf;
  }
  bool to_stdout = strcmp(out_name, "-") == 0;
  FILE *out = to_stdout ? stdout : fopen(out_name, "w");
  if (!out) {
    fprintf(stderr, BOLD RED "wget: failed to open '%s': %s\n" DEFAULT,
            out_name, strerror(errno));
    close(fd);
    return 1;
  }

  // Body bytes that arrived alongside the headers
  size_t total = hdr + hdr_len - body_start;
  bool ok = fwrite(body_start, 1, total, out) == total;

  static char buf[BODY_BUF_SIZE];
  while (ok && (content_length < 0 || (long)total < content_length)) {
    ssize_t n = recv_some(fd, buf, sizeof(buf));
    if (n == 0) {
      break;
    }
    if (n < 0) {
      fprintf(stderr, BOLD RED "wget: read failed: %s\n" DEFAULT,
              strerror(errno));
      ok = false;
      break;
    }
    if (fwrite(buf, 1, (size_t)n, out) != (size_t)n) {
      fprintf(stderr, BOLD RED "wget: failed to write '%s'\n" DEFAULT,
              out_name);
      ok = false;
      break;
    }
    total += (size_t)n;
  }
  close(fd);

  if (ok && content_length >= 0 && (long)total < content_length) {
    fprintf(stderr,
            BOLD RED
            "wget: connection closed early (%zu of %ld bytes)\n" DEFAULT,
            total, content_length);
    ok = false;
  }

  if (!to_stdout) {
    fclose(out);
    if (!ok) {
      unlink(out_name);
      return 1;
    }
  }
  if (ok && !to_stdout) {
    fprintf(stderr, BOLD GREEN "Saved '%s' (%zu bytes)\n" DEFAULT, out_name,
            total);
  }
  return ok ? 0 : 1;
}
