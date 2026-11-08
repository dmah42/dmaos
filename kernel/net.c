#include "net.h"
#include "file.h"
#include "fs.h"
#include "k_errno.h"
#include "kernel.h"
#include "net_driver.h"
#include "process.h"
#include "stdlib.h"
#include "string.h"

#include <lwip/dns.h>
#include <lwip/init.h>
#include <lwip/ip4_addr.h>
#include <lwip/ip_addr.h>
#include <lwip/netif.h>
#include <lwip/pbuf.h>
#include <lwip/tcp.h>
#include <lwip/timeouts.h>
#include <lwip/udp.h>
#include <netif/ethernet.h>

// Network socket definitions
#define AF_INET 2
#define SOCK_STREAM 1
#define SOCK_DGRAM 2

#define MAX_SOCKETS 16
#define MAX_RX_PACKETS 32
#define NET_CONNECT_TIMEOUT_MS 10000
#define NET_DNS_TIMEOUT_MS 5000

struct sockaddr_in {
  uint16_t sin_family;
  uint16_t sin_port;
  struct {
    uint32_t s_addr;
  } sin_addr;
  char sin_zero[8];
};

struct netif dmaos_netif;
struct socket sockets[MAX_SOCKETS];
struct rx_packet rx_packet_pool[MAX_RX_PACKETS];

struct rx_packet *alloc_rx_packet(void) {
  for (int i = 0; i < MAX_RX_PACKETS; ++i) {
    if (!rx_packet_pool[i].in_use) {
      rx_packet_pool[i].in_use = true;
      rx_packet_pool[i].p = NULL;
      rx_packet_pool[i].next = NULL;
      return &rx_packet_pool[i];
    }
  }
  return NULL;
}

void free_rx_packet(struct rx_packet *rx) {
  if (rx) {
    if (rx->p) {
      pbuf_free(rx->p);
      rx->p = NULL;
    }
    rx->in_use = false;
    rx->next = NULL;
  }
}

struct socket *alloc_socket(int type) {
  for (int i = 0; i < MAX_SOCKETS; ++i) {
    if (!sockets[i].in_use) {
      memset(&sockets[i], 0, sizeof(struct socket));
      sockets[i].id = i;
      sockets[i].in_use = true;
      sockets[i].type = type;
      sockets[i].state = SOCK_CLOSED;
      return &sockets[i];
    }
  }
  return NULL;
}

// UDP Receive Callback
void udp_recv_callback(void *arg, _unused struct udp_pcb *pcb, struct pbuf *p,
                       const ip_addr_t *addr, u16_t port) {
  struct socket *s = (struct socket *)arg;
  if (!s || !s->in_use || !p) {
    if (p)
      pbuf_free(p);
    return;
  }

  struct rx_packet *rx = alloc_rx_packet();
  if (!rx) {
    kprintf("udp_recv_callback: out of rx_packets, dropping packet\n");
    pbuf_free(p);
    return;
  }

  rx->p = p;
  rx->addr = *addr;
  rx->port = port;
  rx->next = NULL;

  if (s->rx_tail) {
    s->rx_tail->next = rx;
    s->rx_tail = rx;
  } else {
    s->rx_head = rx;
    s->rx_tail = rx;
  }
}

// TCP Callbacks
err_t tcp_recv_callback(void *arg, _unused struct tcp_pcb *pcb, struct pbuf *p,
                        _unused err_t err) {
  struct socket *s = (struct socket *)arg;
  if (!s || !s->in_use) {
    if (p)
      pbuf_free(p);
    return ERR_VAL;
  }

  if (!p) {
    // Remote closed connection (FIN)
    s->state = SOCK_CLOSED;
    return ERR_OK;
  }

  struct rx_packet *rx = alloc_rx_packet();
  if (!rx) {
    kprintf("tcp_recv_callback: out of rx_packets\n");
    pbuf_free(p);
    return ERR_MEM;
  }

  rx->p = p;
  rx->next = NULL;

  if (s->rx_tail) {
    s->rx_tail->next = rx;
    s->rx_tail = rx;
  } else {
    s->rx_head = rx;
    s->rx_tail = rx;
  }

  return ERR_OK;
}

// lwIP has already freed the PCB when this fires (refused, reset or aborted)
void tcp_err_callback(void *arg, err_t err) {
  struct socket *s = (struct socket *)arg;
  if (!s || !s->in_use) {
    return;
  }
  s->pcb.tcp = NULL;
  s->state = SOCK_CLOSED;
  s->last_err = err;
  wakeup(s); // sys_connect()
}

err_t tcp_connected_callback(void *arg, _unused struct tcp_pcb *pcb,
                             _unused err_t err) {
  struct socket *s = (struct socket *)arg;
  if (s && s->in_use) {
    s->state = SOCK_CONNECTED;
    wakeup(s); // sys_connect()
  }
  return ERR_OK;
}

err_t tcp_accept_callback(void *arg, struct tcp_pcb *newpcb,
                          _unused err_t err) {
  struct socket *listen_sock = (struct socket *)arg;
  if (!listen_sock || !listen_sock->in_use || listen_sock->accept_count >= 8) {
    return ERR_MEM;
  }

  struct socket *child = alloc_socket(SOCK_STREAM);
  if (!child) {
    return ERR_MEM;
  }

  child->pcb.tcp = newpcb;
  child->state = SOCK_CONNECTED;
  tcp_arg(newpcb, child);
  tcp_recv(newpcb, tcp_recv_callback);
  tcp_err(newpcb, tcp_err_callback);

  listen_sock->accept_queue[listen_sock->accept_tail] = child;
  listen_sock->accept_tail = (listen_sock->accept_tail + 1) % 8;
  listen_sock->accept_count++;

  return ERR_OK;
}

void net_init(void) {
  kprintf("net: Initializing network hardware...\n");
  net_driver_init();

  kprintf("net: Initializing lwIP network stack...\n");
  lwip_init();

  ip4_addr_t ipaddr, netmask, gw;
  IP4_ADDR(&ipaddr, 10, 0, 2, 15);
  IP4_ADDR(&netmask, 255, 255, 255, 0);
  IP4_ADDR(&gw, 10, 0, 2, 2);

  netif_add(&dmaos_netif, &ipaddr, &netmask, &gw, NULL, net_driver_netif_init,
            ethernet_input);
  netif_set_default(&dmaos_netif);
  netif_set_up(&dmaos_netif);
  netif_set_link_up(&dmaos_netif);

  memset(sockets, 0, sizeof(sockets));
  memset(rx_packet_pool, 0, sizeof(rx_packet_pool));

  dns_init();
  ip_addr_t default_dns;
  IP4_ADDR(&default_dns, 10, 0, 2, 3);
  dns_setserver(0, &default_dns);

  kprintf("net: lwIP initialized with IP 10.0.2.15/24 (Gateway: 10.0.2.2)\n");
}

void net_poll(void) {
  net_driver_poll(&dmaos_netif);
  sys_check_timeouts();
}

int net_send(const uint8_t *buf, uint32_t len) {
  return net_driver_send(buf, len);
}

int net_recv(uint8_t *buf, uint32_t len) { return net_driver_recv(buf, len); }

// ---------------------------------------------------------------------------
// BSD Socket Syscall Implementations
// ---------------------------------------------------------------------------

int sys_socket(int domain, int type, _unused int protocol) {
  if (domain != AF_INET) {
    return ERR_INVALID_ARGUMENT;
  }
  if (type != SOCK_STREAM && type != SOCK_DGRAM) {
    return ERR_INVALID_ARGUMENT;
  }

  struct socket *s = alloc_socket(type);
  if (!s) {
    return ERR_NO_MEMORY;
  }

  if (type == SOCK_DGRAM) {
    s->pcb.udp = udp_new();
    if (!s->pcb.udp) {
      s->in_use = false;
      return ERR_NO_MEMORY;
    }
    udp_recv(s->pcb.udp, udp_recv_callback, s);
  } else if (type == SOCK_STREAM) {
    s->pcb.tcp = tcp_new();
    if (!s->pcb.tcp) {
      s->in_use = false;
      return ERR_NO_MEMORY;
    }
    tcp_arg(s->pcb.tcp, s);
    tcp_recv(s->pcb.tcp, tcp_recv_callback);
    tcp_err(s->pcb.tcp, tcp_err_callback);
  }

  struct File *f = file_alloc();
  if (!f) {
    net_sock_close(s);
    return ERR_NO_SPACE;
  }

  struct Process *proc = get_current_process();
  int user_fd = -1;
  for (int i = 0; i < NUM_FILES_PER_PROCESS; ++i) {
    if (proc->ofile[i] == NULL) {
      proc->ofile[i] = f;
      user_fd = i;
      break;
    }
  }

  if (user_fd < 0) {
    file_close(f);
    net_sock_close(s);
    return ERR_NO_SPACE;
  }

  f->type = FD_SOCKET;
  f->sock = s;
  f->readable = true;
  f->writable = true;

  return user_fd;
}

int sys_bind(int fd, const void *addr, uint32_t addrlen) {
  if (addrlen < sizeof(struct sockaddr_in) || !addr) {
    return ERR_INVALID_ARGUMENT;
  }

  struct Process *proc = get_current_process();
  if (fd < 0 || fd >= NUM_FILES_PER_PROCESS || proc->ofile[fd] == NULL) {
    return ERR_BAD_FILE;
  }
  struct File *f = proc->ofile[fd];
  if (f->type != FD_SOCKET || f->sock == NULL) {
    return ERR_BAD_FILE;
  }

  struct socket *s = f->sock;
  if (!s->in_use) {
    return ERR_BAD_FILE;
  }

  const struct sockaddr_in *sin = (const struct sockaddr_in *)addr;
  ip_addr_t ip;
  ip_addr_set_ip4_u32(&ip, sin->sin_addr.s_addr);
  u16_t port =
      ((sin->sin_port << 8) & 0xFF00) | ((sin->sin_port >> 8) & 0x00FF);

  err_t err = ERR_OK;
  if (s->type == SOCK_DGRAM) {
    err = udp_bind(s->pcb.udp, &ip, port);
  } else if (s->type == SOCK_STREAM) {
    if (!s->pcb.tcp) {
      return ERR_INVALID_ARGUMENT;
    }
    err = tcp_bind(s->pcb.tcp, &ip, port);
  }

  if (err != ERR_OK) {
    return ERR_PERMISSION_DENIED;
  }

  s->state = SOCK_BOUND;
  return 0;
}

int sys_connect(int fd, const void *addr, uint32_t addrlen) {
  if (addrlen < sizeof(struct sockaddr_in) || !addr) {
    return ERR_INVALID_ARGUMENT;
  }

  struct Process *proc = get_current_process();
  if (fd < 0 || fd >= NUM_FILES_PER_PROCESS || proc->ofile[fd] == NULL) {
    return ERR_BAD_FILE;
  }
  struct File *f = proc->ofile[fd];
  if (f->type != FD_SOCKET || f->sock == NULL) {
    return ERR_BAD_FILE;
  }

  struct socket *s = f->sock;
  if (!s->in_use) {
    return ERR_BAD_FILE;
  }

  const struct sockaddr_in *sin = (const struct sockaddr_in *)addr;
  ip_addr_t ip;
  ip_addr_set_ip4_u32(&ip, sin->sin_addr.s_addr);
  u16_t port =
      ((sin->sin_port << 8) & 0xFF00) | ((sin->sin_port >> 8) & 0x00FF);

  if (s->type == SOCK_DGRAM) {
    udp_connect(s->pcb.udp, &ip, port);
    s->state = SOCK_CONNECTED;
    return 0;
  }

  // TCP connect (synchronous connect with timeout)
  if (!s->pcb.tcp) {
    return ERR_INVALID_ARGUMENT;
  }
  s->state = SOCK_CONNECTING;
  s->last_err = ERR_OK;
  if (tcp_connect(s->pcb.tcp, &ip, port, tcp_connected_callback) != ERR_OK) {
    s->state = SOCK_CLOSED;
    return ERR_IO;
  }

  uint32_t start_ms = uptime_ms();
  while (s->state == SOCK_CONNECTING) {
    net_poll();
    if (s->state != SOCK_CONNECTING) {
      break;
    }
    uint32_t elapsed_ms = uptime_ms() - start_ms;
    if (elapsed_ms > NET_CONNECT_TIMEOUT_MS) {
      tcp_abort(s->pcb.tcp); // fires tcp_err_callback, which clears the PCB
      return ERR_TIMED_OUT;
    }
    // tcp_connected_callback or tcp_err_callback wakes us from net_poll() on
    // the timer tick.
    sleep_on(s, NET_CONNECT_TIMEOUT_MS - elapsed_ms + 1);
  }

  // A peer may connect and send FIN within one poll; the PCB survives that
  if (s->state == SOCK_CONNECTED || (s->pcb.tcp && s->last_err == ERR_OK)) {
    return 0;
  }
  return (s->last_err == ERR_RST) ? ERR_CONNECTION_REFUSED : ERR_IO;
}

int sys_listen(int fd, int backlog) {
  struct Process *proc = get_current_process();
  if (fd < 0 || fd >= NUM_FILES_PER_PROCESS || proc->ofile[fd] == NULL) {
    return ERR_BAD_FILE;
  }
  struct File *f = proc->ofile[fd];
  if (f->type != FD_SOCKET || f->sock == NULL) {
    return ERR_BAD_FILE;
  }

  struct socket *s = f->sock;
  if (!s->in_use || s->type != SOCK_STREAM) {
    return ERR_INVALID_ARGUMENT;
  }

  if (!s->pcb.tcp) {
    return ERR_INVALID_ARGUMENT;
  }
  struct tcp_pcb *listen_pcb =
      tcp_listen_with_backlog(s->pcb.tcp, backlog > 0 ? backlog : 4);
  if (!listen_pcb) {
    return ERR_NO_MEMORY;
  }

  s->pcb.tcp = listen_pcb;
  s->state = SOCK_LISTENING;
  tcp_accept(listen_pcb, tcp_accept_callback);
  return 0;
}

int sys_accept(int fd, _unused void *addr, _unused uint32_t *addrlen) {
  struct Process *proc = get_current_process();
  if (fd < 0 || fd >= NUM_FILES_PER_PROCESS || proc->ofile[fd] == NULL) {
    return ERR_BAD_FILE;
  }
  struct File *f = proc->ofile[fd];
  if (f->type != FD_SOCKET || f->sock == NULL) {
    return ERR_BAD_FILE;
  }

  struct socket *s = f->sock;
  if (!s->in_use || s->type != SOCK_STREAM || s->state != SOCK_LISTENING) {
    return ERR_INVALID_ARGUMENT;
  }

  net_poll();
  if (s->accept_count == 0) {
    return ERR_NOT_FOUND;
  }

  struct socket *child = s->accept_queue[s->accept_head];
  s->accept_head = (s->accept_head + 1) % 8;
  s->accept_count--;

  struct File *child_file = file_alloc();
  if (!child_file) {
    net_sock_close(child);
    return ERR_NO_SPACE;
  }

  int user_fd = -1;
  for (int i = 0; i < NUM_FILES_PER_PROCESS; ++i) {
    if (proc->ofile[i] == NULL) {
      proc->ofile[i] = child_file;
      user_fd = i;
      break;
    }
  }

  if (user_fd < 0) {
    file_close(child_file);
    net_sock_close(child);
    return ERR_NO_SPACE;
  }

  child_file->type = FD_SOCKET;
  child_file->sock = child;
  child_file->readable = true;
  child_file->writable = true;

  return user_fd;
}

int sys_sendto(int fd, const void *buf, uint32_t len, _unused int flags,
               const void *addr, uint32_t addrlen) {
  struct Process *proc = get_current_process();
  if (fd < 0 || fd >= NUM_FILES_PER_PROCESS || proc->ofile[fd] == NULL) {
    return ERR_BAD_FILE;
  }
  struct File *f = proc->ofile[fd];
  if (f->type != FD_SOCKET || f->sock == NULL) {
    return ERR_BAD_FILE;
  }

  struct socket *s = f->sock;
  if (!s->in_use) {
    return ERR_BAD_FILE;
  }

  if (s->type == SOCK_DGRAM) {
    ip_addr_t dst_ip;
    u16_t dst_port = 0;

    if (addr && addrlen >= sizeof(struct sockaddr_in)) {
      const struct sockaddr_in *sin = (const struct sockaddr_in *)addr;
      ip_addr_set_ip4_u32(&dst_ip, sin->sin_addr.s_addr);
      dst_port =
          ((sin->sin_port << 8) & 0xFF00) | ((sin->sin_port >> 8) & 0x00FF);
    } else if (s->pcb.udp->remote_port != 0) {
      dst_ip = s->pcb.udp->remote_ip;
      dst_port = s->pcb.udp->remote_port;
    } else {
      return ERR_INVALID_ARGUMENT;
    }

    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)len, PBUF_RAM);
    if (!p) {
      return ERR_NO_MEMORY;
    }

    pbuf_take(p, buf, (u16_t)len);
    err_t err = udp_sendto(s->pcb.udp, p, &dst_ip, dst_port);
    pbuf_free(p);

    if (err != ERR_OK) {
      return ERR_IO;
    }

    net_poll();
    return (int)len;
  } else if (s->type == SOCK_STREAM) {
    if (!s->pcb.tcp) {
      return ERR_BROKEN_PIPE;
    }
    err_t err = tcp_write(s->pcb.tcp, buf, (u16_t)len, TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK) {
      return ERR_IO;
    }
    tcp_output(s->pcb.tcp);
    net_poll();
    return (int)len;
  }

  return ERR_NOT_IMPLEMENTED;
}

int sys_recvfrom(int fd, void *buf, uint32_t len, _unused int flags, void *addr,
                 uint32_t *addrlen) {
  struct Process *proc = get_current_process();
  if (fd < 0 || fd >= NUM_FILES_PER_PROCESS || proc->ofile[fd] == NULL) {
    return ERR_BAD_FILE;
  }
  struct File *f = proc->ofile[fd];
  if (f->type != FD_SOCKET || f->sock == NULL) {
    return ERR_BAD_FILE;
  }

  struct socket *s = f->sock;
  if (!s->in_use) {
    return ERR_BAD_FILE;
  }

  net_poll();

  if (!s->rx_head) {
    return ERR_NOT_FOUND;
  }

  struct rx_packet *rx = s->rx_head;
  u16_t copied = pbuf_copy_partial(rx->p, buf, (u16_t)len, 0);

  if (addr && addrlen && *addrlen >= sizeof(struct sockaddr_in)) {
    struct sockaddr_in *sin = (struct sockaddr_in *)addr;
    sin->sin_family = AF_INET;
    sin->sin_port = ((rx->port << 8) & 0xFF00) | ((rx->port >> 8) & 0x00FF);
    sin->sin_addr.s_addr = ip_addr_get_ip4_u32(&rx->addr);
    *addrlen = sizeof(struct sockaddr_in);
  }

  s->rx_head = rx->next;
  if (!s->rx_head) {
    s->rx_tail = NULL;
  }

  free_rx_packet(rx);
  return (int)copied;
}

int net_sock_read(struct socket *s, void *buf, size_t count) {
  if (!s || !s->in_use) {
    return ERR_BAD_FILE;
  }

  net_poll();

  if (!s->rx_head) {
    if (s->state == SOCK_CLOSED) {
      return 0; // EOF
    }
    return ERR_NOT_FOUND;
  }

  struct rx_packet *rx = s->rx_head;
  u16_t avail = rx->p->tot_len - s->rx_offset;
  u16_t to_copy = (avail < count) ? avail : (u16_t)count;

  pbuf_copy_partial(rx->p, buf, to_copy, s->rx_offset);
  s->rx_offset += to_copy;

  if (s->type == SOCK_STREAM && s->pcb.tcp) {
    tcp_recved(s->pcb.tcp, to_copy);
  }

  if (s->rx_offset >= rx->p->tot_len) {
    s->rx_head = rx->next;
    if (!s->rx_head) {
      s->rx_tail = NULL;
    }
    s->rx_offset = 0;
    free_rx_packet(rx);
  }

  return (int)to_copy;
}

int net_sock_write(struct socket *s, const void *buf, size_t count) {
  if (!s || !s->in_use) {
    return ERR_BAD_FILE;
  }

  if (s->type == SOCK_STREAM) {
    if (!s->pcb.tcp) {
      return ERR_BROKEN_PIPE;
    }
    err_t err = tcp_write(s->pcb.tcp, buf, (u16_t)count, TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK) {
      return ERR_IO;
    }
    tcp_output(s->pcb.tcp);
    return (int)count;
  } else if (s->type == SOCK_DGRAM) {
    if (s->pcb.udp->remote_port == 0) {
      return ERR_INVALID_ARGUMENT;
    }
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)count, PBUF_RAM);
    if (!p)
      return ERR_NO_MEMORY;
    pbuf_take(p, buf, (u16_t)count);
    err_t err = udp_send(s->pcb.udp, p);
    pbuf_free(p);
    return (err == ERR_OK) ? (int)count : ERR_IO;
  }

  return ERR_NOT_IMPLEMENTED;
}

void net_sock_close(struct socket *s) {
  if (!s || !s->in_use) {
    return;
  }

  if (s->type == SOCK_DGRAM && s->pcb.udp) {
    udp_remove(s->pcb.udp);
    s->pcb.udp = NULL;
  } else if (s->type == SOCK_STREAM && s->pcb.tcp) {
    tcp_arg(s->pcb.tcp, NULL);
    if (s->state != SOCK_LISTENING) {
      tcp_recv(s->pcb.tcp, NULL);
      tcp_sent(s->pcb.tcp, NULL);
      tcp_err(s->pcb.tcp, NULL);
    } else {
      tcp_accept(s->pcb.tcp, NULL);
    }
    if (tcp_close(s->pcb.tcp) != ERR_OK) {
      tcp_abort(s->pcb.tcp);
    }
    s->pcb.tcp = NULL;
  }

  struct rx_packet *curr = s->rx_head;
  while (curr) {
    struct rx_packet *next = curr->next;
    free_rx_packet(curr);
    curr = next;
  }

  s->rx_head = NULL;
  s->rx_tail = NULL;
  s->in_use = false;
  s->state = SOCK_CLOSED;
}

// ---------------------------------------------------------------------------
// DNS Resolution
// ---------------------------------------------------------------------------

bool net_cfg_loaded = false;

void net_load_config(void) {
  if (net_cfg_loaded) {
    return;
  }
  net_cfg_loaded = true;

  struct inode *ip = namei("/cfg/net.cfg");
  if (!ip) {
    return;
  }

  uint8_t buf[256];
  memset(buf, 0, sizeof(buf));
  int n = readi(ip, buf, 0, sizeof(buf) - 1);
  iput(ip);

  if (n <= 0) {
    return;
  }

  // Parse NAMESERVER=<ip>
  char *ns = strstr((const char *)buf, "NAMESERVER=");
  if (ns) {
    ns += strlen("NAMESERVER=");
    while (*ns == ' ' || *ns == '\t') {
      ++ns;
    }
    char ip_str[32];
    int len = 0;
    while (ns[len] && ns[len] != '\r' && ns[len] != '\n' && ns[len] != ' ' &&
           len < (int)sizeof(ip_str) - 1) {
      ip_str[len] = ns[len];
      ++len;
    }
    ip_str[len] = '\0';

    ip_addr_t server_ip;
    if (ip4addr_aton(ip_str, &server_ip)) {
      dns_setserver(0, &server_ip);
      kprintf("net: DNS nameserver set to %s from /cfg/net.cfg\n", ip_str);
    }
  }
}

struct dns_query_state {
  bool done;
  err_t err;
  ip_addr_t addr;
};

void dns_found_cb(_unused const char *name, const ip_addr_t *ipaddr,
                  void *callback_arg) {
  struct dns_query_state *state = (struct dns_query_state *)callback_arg;
  if (ipaddr != NULL) {
    state->addr = *ipaddr;
    state->err = ERR_OK;
  } else {
    state->err = ERR_VAL;
  }
  state->done = true;
  wakeup(state); // sys_dns_lookup()
}

int sys_dns_lookup(const char *name, uint32_t *ip_out) {
  if (!name || !ip_out) {
    return ERR_INVALID_ARGUMENT;
  }

  net_load_config();

  ip_addr_t resolved;
  struct dns_query_state state;
  state.done = false;
  state.err = ERR_INPROGRESS;

  err_t err = dns_gethostbyname(name, &resolved, dns_found_cb, &state);
  if (err == ERR_OK) {
    *ip_out = ip_addr_get_ip4_u32(&resolved);
    return 0;
  } else if (err != ERR_INPROGRESS) {
    return ERR_NOT_FOUND;
  }

  uint32_t start_ms = uptime_ms();
  while (!state.done) {
    net_poll();
    if (state.done) {
      break;
    }
    uint32_t elapsed_ms = uptime_ms() - start_ms;
    if (elapsed_ms > NET_DNS_TIMEOUT_MS) {
      return ERR_IO;
    }
    // dns_found_cb wakes us from net_poll() on the timer tick.
    sleep_on(&state, NET_DNS_TIMEOUT_MS - elapsed_ms + 1);
  }

  if (state.err == ERR_OK) {
    *ip_out = ip_addr_get_ip4_u32(&state.addr);
    return 0;
  }

  return ERR_NOT_FOUND;
}