# ----------------- lwIP & Kernel Adapter Rules -----------------

LWIP_DIR := third_party/lwip/src

LWIP_SRCS := $(LWIP_DIR)/core/init.c \
             $(LWIP_DIR)/core/def.c \
             $(LWIP_DIR)/core/inet_chksum.c \
             $(LWIP_DIR)/core/ip.c \
             $(LWIP_DIR)/core/mem.c \
             $(LWIP_DIR)/core/memp.c \
             $(LWIP_DIR)/core/netif.c \
             $(LWIP_DIR)/core/pbuf.c \
             $(LWIP_DIR)/core/stats.c \
             $(LWIP_DIR)/core/sys.c \
             $(LWIP_DIR)/core/tcp.c \
             $(LWIP_DIR)/core/tcp_in.c \
             $(LWIP_DIR)/core/tcp_out.c \
             $(LWIP_DIR)/core/timeouts.c \
             $(LWIP_DIR)/core/udp.c \
             $(LWIP_DIR)/core/ipv4/etharp.c \
             $(LWIP_DIR)/core/ipv4/icmp.c \
             $(LWIP_DIR)/core/ipv4/ip4.c \
             $(LWIP_DIR)/core/ipv4/ip4_addr.c \
             $(LWIP_DIR)/core/ipv4/ip4_frag.c \
             $(LWIP_DIR)/netif/ethernet.c

LWIP_PORT_SRCS := $(KERNEL_DIR)/lwip/netif_virtio.c

LWIP_OBJS := $(patsubst third_party/lwip/%.c, $(BUILD_DIR)/third_party/lwip/%.o, $(LWIP_SRCS))
LWIP_PORT_OBJS := $(patsubst $(KERNEL_DIR)/lwip/%.c, $(BUILD_DIR)/kernel/lwip/%.o, $(LWIP_PORT_SRCS))

$(BUILD_DIR)/kernel/lwip/%.o: $(KERNEL_DIR)/lwip/%.c
	@mkdir -p $(dir $@)
	$(cc) $(cflags) $(KERNEL_INCLUDES) -MMD -MP -c -o $@ $<

$(BUILD_DIR)/third_party/lwip/%.o: third_party/lwip/%.c
	@mkdir -p $(dir $@)
	$(cc) $(cflags) $(KERNEL_INCLUDES) -Wno-unused-parameter -MMD -MP -c -o $@ $<
