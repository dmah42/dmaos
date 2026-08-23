#pragma once

#include <stdint.h>
#include <netinet/in.h>

static inline uint16_t htons(uint16_t n) {
  return (uint16_t)((n << 8) | (n >> 8));
}

static inline uint16_t ntohs(uint16_t n) {
  return htons(n);
}

static inline uint32_t htonl(uint32_t n) {
  return ((n & 0x000000FF) << 24) |
         ((n & 0x0000FF00) << 8)  |
         ((n & 0x00FF0000) >> 8)  |
         ((n & 0xFF000000) >> 24);
}

static inline uint32_t ntohl(uint32_t n) {
  return htonl(n);
}

in_addr_t inet_addr(const char *cp);
int inet_aton(const char *cp, struct in_addr *inp);
char *inet_ntoa(struct in_addr in);
