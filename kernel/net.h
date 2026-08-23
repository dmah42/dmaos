#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <lwip/ip_addr.h>
#include <lwip/pbuf.h>
#include <lwip/tcp.h>
#include <lwip/udp.h>

struct rx_packet {
  bool in_use;
  struct pbuf *p;
  ip_addr_t addr;
  u16_t port;
  struct rx_packet *next;
};

enum socket_state {
  SOCK_CLOSED = 0,
  SOCK_BOUND,
  SOCK_LISTENING,
  SOCK_CONNECTING,
  SOCK_CONNECTED,
};

struct socket {
  int id;
  bool in_use;
  int type; // SOCK_STREAM or SOCK_DGRAM
  enum socket_state state;

  union {
    struct udp_pcb *udp;
    struct tcp_pcb *tcp;
  } pcb;

  struct rx_packet *rx_head;
  struct rx_packet *rx_tail;
  size_t rx_offset; // for stream reads in TCP

  // For TCP listening sockets
  struct socket *accept_queue[8];
  int accept_head;
  int accept_tail;
  int accept_count;
};

void net_init(void);
void net_poll(void);

// Raw packet fallback / legacy API
int net_send(const uint8_t *buf, uint32_t len);
int net_recv(uint8_t *buf, uint32_t len);

// BSD Socket System Calls (Kernel handlers)
int sys_socket(int domain, int type, int protocol);
int sys_bind(int fd, const void *addr, uint32_t addrlen);
int sys_connect(int fd, const void *addr, uint32_t addrlen);
int sys_listen(int fd, int backlog);
int sys_accept(int fd, void *addr, uint32_t *addrlen);
int sys_sendto(int fd, const void *buf, uint32_t len, int flags,
               const void *addr, uint32_t addrlen);
int sys_recvfrom(int fd, void *buf, uint32_t len, int flags, void *addr,
                 uint32_t *addrlen);
int sys_dns_lookup(const char *name, uint32_t *ip_out);

// Socket I/O functions
int net_sock_read(struct socket *sock, void *buf, size_t count);
int net_sock_write(struct socket *sock, const void *buf, size_t count);
void net_sock_close(struct socket *sock);