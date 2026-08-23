#include "netif_virtio.h"
#include "kernel.h"
#include "stdlib.h"
#include "virtio.h"

#include <lwip/etharp.h>
#include <lwip/pbuf.h>

err_t virtio_netif_output(_unused struct netif *netif, struct pbuf *p) {
  uint8_t buffer[1600];
  if (p->tot_len > sizeof(buffer)) {
    kprintf("virtio_netif_output: packet too large (%d bytes)\n", p->tot_len);
    return ERR_BUF;
  }

  u16_t copied = pbuf_copy_partial(p, buffer, p->tot_len, 0);
  if (copied != p->tot_len) {
    kprintf("virtio_netif_output: pbuf_copy_partial failed\n");
    return ERR_BUF;
  }

  int ret = virtio_net_send(buffer, copied);
  if (ret < 0) {
    return ERR_IF;
  }

  return ERR_OK;
}

err_t virtio_netif_init(struct netif *netif) {
  netif->name[0] = 'e';
  netif->name[1] = 'n';
  netif->output = etharp_output;
  netif->linkoutput = virtio_netif_output;
  netif->mtu = 1500;
  netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;

  // Default guest MAC address matching QEMU virtio net
  netif->hwaddr_len = 6;
  netif->hwaddr[0] = 0x52;
  netif->hwaddr[1] = 0x54;
  netif->hwaddr[2] = 0x00;
  netif->hwaddr[3] = 0x12;
  netif->hwaddr[4] = 0x34;
  netif->hwaddr[5] = 0x57;

  return ERR_OK;
}

void virtio_netif_poll(struct netif *netif) {
  uint8_t rx_buf[1600];
  int len;

  while ((len = virtio_net_recv(rx_buf, sizeof(rx_buf))) > 0) {
    struct pbuf *p = pbuf_alloc(PBUF_RAW, (u16_t)len, PBUF_POOL);
    if (!p) {
      kprintf("virtio_netif_poll: pbuf_alloc failed for %d bytes\n", len);
      continue;
    }

    err_t err = pbuf_take(p, rx_buf, (u16_t)len);
    if (err != ERR_OK) {
      pbuf_free(p);
      continue;
    }

    if (netif->input(p, netif) != ERR_OK) {
      pbuf_free(p);
    }
  }
}

// System uptime in milliseconds for lwIP timers
uint32_t sys_now(void) { return uptime_ms(); }
