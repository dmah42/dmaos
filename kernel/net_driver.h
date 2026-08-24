#pragma once

#include <lwip/err.h>
#include <lwip/netif.h>

void net_driver_init(void);
err_t net_driver_netif_init(struct netif *netif);
void net_driver_poll(struct netif *netif);
int net_driver_send(const uint8_t *buf, uint32_t len);
int net_driver_recv(uint8_t *buf, uint32_t len);
