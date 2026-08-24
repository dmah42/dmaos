#pragma once

#include <stdbool.h>
#include <stdint.h>

#define VIRTIO_BLK0_PADDR 0x10001000
#define VIRTIO_BLK1_PADDR 0x10002000
#define VIRTIO_INPUT_PADDR 0x10003000
#define VIRTIO_NET_PADDR 0x10004000

void virtio_blk_init(void);
uint32_t virtio_blk_sectors(uint32_t dev);

void virtio_input_init();
bool virtio_input_poll_event(uint16_t *code, int *pressed);

void virtio_net_init();
int virtio_net_send(const uint8_t *buf, uint32_t len);
int virtio_net_recv(uint8_t *buf, uint32_t len);