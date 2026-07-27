#include "net.h"
#include "virtio.h"

void net_init() { virtio_net_init(); }