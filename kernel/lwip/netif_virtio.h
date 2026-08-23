#pragma once

#include "lwip/netif.h"

err_t virtio_netif_init(struct netif *netif);
void virtio_netif_poll(struct netif *netif);
