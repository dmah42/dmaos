#include "net.h"

#include "virtio.h"

void net_init() { virtio_net_init(); }

int net_send(const uint8_t *buf, uint32_t len) {
  return virtio_net_send(buf, len);
}

int net_recv(uint8_t *buf, uint32_t len) { return virtio_net_recv(buf, len); }