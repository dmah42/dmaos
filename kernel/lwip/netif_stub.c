#include "net_driver.h"
#include "kernel.h"
#include "stdlib.h"

void net_driver_init(void) {
  kprintf("net: network hardware driver stub (no physical Ethernet card active)\n");
}

err_t net_driver_netif_init(struct netif *netif) {
  netif->name[0] = 'e';
  netif->name[1] = '0';
  netif->mtu = 1500;
  return ERR_OK;
}

void net_driver_poll(struct netif *netif) {
  (void)netif;
}

int net_driver_send(const uint8_t *buf, uint32_t len) {
  (void)buf;
  (void)len;
  return -1;
}

int net_driver_recv(uint8_t *buf, uint32_t len) {
  (void)buf;
  (void)len;
  return 0;
}
