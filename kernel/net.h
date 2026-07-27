#pragma once

#include <stdint.h>

void net_init();
int net_send(const uint8_t *buf, uint32_t len);
int net_recv(uint8_t *buf, uint32_t len);