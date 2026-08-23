#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "colors.h"
#include "user.h"

// Protocol Constants
#define ETHERTYPE_IPV4 0x0800
#define ETHERTYPE_ARP 0x0806

#define ARP_OP_REQUEST 1
#define ARP_OP_REPLY 2

#define IP_PROTO_UDP 17

// Default Guest MAC address
static const uint8_t GUEST_MAC[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x57};

// Header Structures (packed)
struct eth_hdr {
  uint8_t dst_mac[6];
  uint8_t src_mac[6];
  uint16_t ethertype;
} __attribute__((packed));

struct arp_hdr {
  uint16_t hw_type;
  uint16_t proto_type;
  uint8_t hw_size;
  uint8_t proto_size;
  uint16_t opcode;
  uint8_t sender_mac[6];
  uint8_t sender_ip[4];
  uint8_t target_mac[6];
  uint8_t target_ip[4];
} __attribute__((packed));

struct ipv4_hdr {
  uint8_t ihl_version;
  uint8_t tos;
  uint16_t total_len;
  uint16_t id;
  uint16_t frag_off;
  uint8_t ttl;
  uint8_t protocol;
  uint16_t checksum;
  uint8_t src_ip[4];
  uint8_t dst_ip[4];
} __attribute__((packed));

struct udp_hdr {
  uint16_t src_port;
  uint16_t dst_port;
  uint16_t length;
  uint16_t checksum;
} __attribute__((packed));

// Endianness Helpers
static inline uint16_t ntohs(uint16_t net16) {
  return (uint16_t)((net16 << 8) | (net16 >> 8));
}

static inline uint16_t htons(uint16_t host16) { return ntohs(host16); }

// MAC printing helper
static void print_mac(const uint8_t mac[6]) {
  printf("%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3],
         mac[4], mac[5]);
}

// IP printing helper
static void print_ip(const uint8_t ip[4]) {
  printf("%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
}

// Handler for ARP frames
static bool handle_arp_frame(uint8_t *buf, size_t len) {
  if (len < sizeof(struct eth_hdr) + sizeof(struct arp_hdr)) {
    return false;
  }

  struct eth_hdr *eth = (struct eth_hdr *)buf;
  struct arp_hdr *arp = (struct arp_hdr *)(buf + sizeof(struct eth_hdr));

  uint16_t opcode = ntohs(arp->opcode);

  printf("  [ARP Frame] Opcode: %d (%s)\n", opcode,
         (opcode == ARP_OP_REQUEST) ? "Request" : "Reply");
  printf("  Sender IP: ");
  print_ip(arp->sender_ip);
  printf(" (MAC: ");
  print_mac(arp->sender_mac);
  printf(")\n");
  printf("  Target IP: ");
  print_ip(arp->target_ip);
  printf("\n");

  if (opcode == ARP_OP_REQUEST) {
    printf(BOLD YELLOW
           "  --> Formatting ARP Reply for QEMU SLIRP...\n" DEFAULT);

    // Save gateway IP and MAC before overwriting
    uint8_t gateway_ip[4];
    uint8_t gateway_mac[6];
    for (int i = 0; i < 4; ++i)
      gateway_ip[i] = arp->sender_ip[i];
    for (int i = 0; i < 6; ++i)
      gateway_mac[i] = arp->sender_mac[i];

    // Ethernet Header: Dst MAC = gateway MAC, Src MAC = guest MAC
    for (int i = 0; i < 6; ++i) {
      eth->dst_mac[i] = gateway_mac[i];
      eth->src_mac[i] = GUEST_MAC[i];
    }

    // ARP Header: Change to Reply (2)
    arp->opcode = htons(ARP_OP_REPLY);

    // Sender = Guest (10.0.2.15 / GUEST_MAC)
    for (int i = 0; i < 6; ++i)
      arp->sender_mac[i] = GUEST_MAC[i];
    for (int i = 0; i < 4; ++i)
      arp->sender_ip[i] = arp->target_ip[i]; // Guest IP

    // Target = Gateway (10.0.2.2 / gateway_mac)
    for (int i = 0; i < 6; ++i)
      arp->target_mac[i] = gateway_mac[i];
    for (int i = 0; i < 4; ++i)
      arp->target_ip[i] = gateway_ip[i];

    int tx_res = net_send(buf, len);
    if (tx_res < 0) {
      printf(BOLD RED "  Error sending ARP Reply\n" DEFAULT);
    } else {
      printf(BOLD GREEN "  [ARP Reply Transmitted] %d bytes\n" DEFAULT, tx_res);
    }
  }

  // Return false to keep main loop listening for the subsequent UDP packet
  return false;
}

// Handler for IPv4 / UDP frames
static bool handle_udp_frame(uint8_t *buf, size_t len) {
  size_t min_len =
      sizeof(struct eth_hdr) + sizeof(struct ipv4_hdr) + sizeof(struct udp_hdr);
  if (len < min_len) {
    return false;
  }

  struct eth_hdr *eth = (struct eth_hdr *)buf;
  struct ipv4_hdr *ip = (struct ipv4_hdr *)(buf + sizeof(struct eth_hdr));
  struct udp_hdr *udp = (struct udp_hdr *)(buf + sizeof(struct eth_hdr) +
                                           sizeof(struct ipv4_hdr));

  uint16_t src_port = ntohs(udp->src_port);
  uint16_t dst_port = ntohs(udp->dst_port);

  printf("  [IPv4 UDP Packet Received]\n");
  printf("  Source IP: ");
  print_ip(ip->src_ip);
  printf(":%d\n", src_port);
  printf("  Dest IP:   ");
  print_ip(ip->dst_ip);
  printf(":%d\n", dst_port);

  size_t payload_offset = min_len;
  printf("  Payload:   \"");
  for (size_t p = payload_offset; p < len && p < payload_offset + 64; ++p) {
    char c = (char)buf[p];
    if (c >= 32 && c <= 126)
      printf("%c", c);
    else
      printf(".");
  }
  printf("\"\n");

  printf(BOLD YELLOW "  --> Swapping Headers for UDP Echo Reply...\n" DEFAULT);

  // 1. Swap Ethernet MACs
  for (int i = 0; i < 6; ++i) {
    uint8_t tmp = eth->dst_mac[i];
    eth->dst_mac[i] = eth->src_mac[i];
    eth->src_mac[i] = tmp;
  }

  // 2. Swap IPv4 Addresses
  for (int i = 0; i < 4; ++i) {
    uint8_t tmp = ip->src_ip[i];
    ip->src_ip[i] = ip->dst_ip[i];
    ip->dst_ip[i] = tmp;
  }

  // 3. Swap UDP Ports
  uint16_t tmp_port = udp->src_port;
  udp->src_port = udp->dst_port;
  udp->dst_port = tmp_port;

  // Clear UDP checksum (optional in IPv4)
  udp->checksum = 0;

  int tx_res = net_send(buf, len);
  if (tx_res < 0) {
    printf(BOLD RED "  Error sending UDP Echo\n" DEFAULT);
  } else {
    printf(BOLD GREEN "  [UDP Echo Transmitted] %d bytes\n" DEFAULT, tx_res);
  }

  // Return true to terminate nettest after successful UDP echo
  return true;
}

int main() {
  printf(BOLD "--- Net Test Utility Started ---\n" DEFAULT);
  printf("Awaiting raw network packet from host network...\n");

  uint8_t packet_buffer[1600];

  // Poll for up to 10 seconds (600 iterations * 16ms)
  for (int i = 0; i < 600; ++i) {
    int rx_bytes = net_recv(packet_buffer, sizeof(packet_buffer));

    if (rx_bytes < 0) {
      sleep_ms(16);
      continue;
    }

    if (rx_bytes < (int)sizeof(struct eth_hdr)) {
      continue;
    }

    struct eth_hdr *eth = (struct eth_hdr *)packet_buffer;
    uint16_t ethertype = ntohs(eth->ethertype);

    printf(BOLD GREEN
           "\n[Packet Received] Length: %d bytes (EtherType: 0x%04x)\n" DEFAULT,
           rx_bytes, ethertype);

    if (ethertype == ETHERTYPE_ARP) {
      handle_arp_frame(packet_buffer, (size_t)rx_bytes);
    } else if (ethertype == ETHERTYPE_IPV4) {
      struct ipv4_hdr *ip =
          (struct ipv4_hdr *)(packet_buffer + sizeof(struct eth_hdr));
      if (ip->protocol == IP_PROTO_UDP) {
        bool finished = handle_udp_frame(packet_buffer, (size_t)rx_bytes);
        if (finished) {
          return 0;
        }
      }
    }
  }

  printf(BOLD RED "\nNo UDP packet received within timeout period.\n" DEFAULT);
  return 1;
}
