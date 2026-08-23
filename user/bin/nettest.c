#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "colors.h"
#include "user.h"

#define TEST_PORT 9999

static int run_udp_test(void) {
  printf(BOLD "--- dmaOS UDP Socket Test (lwIP) ---\n" DEFAULT);
  printf("Creating UDP socket...\n");

  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    printf(BOLD RED "Failed to create socket (err = %d)\n" DEFAULT, fd);
    return 1;
  }

  struct sockaddr_in serv_addr;
  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(TEST_PORT);
  serv_addr.sin_addr.s_addr = INADDR_ANY;

  if (bind(fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
    printf(BOLD RED "Failed to bind socket to port %d\n" DEFAULT, TEST_PORT);
    close(fd);
    return 1;
  }

  printf(BOLD GREEN "Socket bound to UDP port %d\n" DEFAULT, TEST_PORT);
  printf("Awaiting UDP packet from host (timeout 10s)...\n");

  char buf[512];
  struct sockaddr_in client_addr;
  socklen_t addr_len = sizeof(client_addr);

  // Poll for up to 10 seconds (600 iterations * 16ms)
  for (int i = 0; i < 600; ++i) {
    memset(&client_addr, 0, sizeof(client_addr));
    addr_len = sizeof(client_addr);

    ssize_t rx_bytes = recvfrom(fd, buf, sizeof(buf) - 1, 0,
                                (struct sockaddr *)&client_addr, &addr_len);

    if (rx_bytes > 0) {
      buf[rx_bytes] = '\0';
      printf(BOLD GREEN "\n[UDP Packet Received] %d bytes from %s:%d\n" DEFAULT,
             (int)rx_bytes, inet_ntoa(client_addr.sin_addr),
             ntohs(client_addr.sin_port));
      printf("  Payload: \"%s\"\n", buf);

      printf(BOLD YELLOW "  --> Sending UDP Echo Reply back...\n" DEFAULT);
      ssize_t tx_bytes = sendto(fd, buf, (size_t)rx_bytes, 0,
                                (struct sockaddr *)&client_addr, addr_len);
      if (tx_bytes < 0) {
        printf(BOLD RED "  Error sending echo reply\n" DEFAULT);
      } else {
        printf(BOLD GREEN "  [UDP Echo Transmitted] %d bytes\n" DEFAULT,
               (int)tx_bytes);
      }

      sleep_ms(100);
      close(fd);
      return 0;
    }

    sleep_ms(16);
  }

  printf(BOLD RED "\nNo UDP packet received within timeout period.\n" DEFAULT);
  close(fd);
  return 1;
}

static int run_tcp_test(void) {
  printf(BOLD "--- dmaOS TCP Socket Test (lwIP) ---\n" DEFAULT);
  printf("Creating TCP listening socket...\n");

  int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd < 0) {
    printf(BOLD RED "Failed to create socket (err = %d)\n" DEFAULT, listen_fd);
    return 1;
  }

  struct sockaddr_in serv_addr;
  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(TEST_PORT);
  serv_addr.sin_addr.s_addr = INADDR_ANY;

  if (bind(listen_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
    printf(BOLD RED "Failed to bind socket to port %d\n" DEFAULT, TEST_PORT);
    close(listen_fd);
    return 1;
  }

  if (listen(listen_fd, 2) < 0) {
    printf(BOLD RED "Failed to listen on socket\n" DEFAULT);
    close(listen_fd);
    return 1;
  }

  printf(BOLD GREEN "TCP Server listening on port %d\n" DEFAULT, TEST_PORT);
  printf("Awaiting TCP connection from host (timeout 15s)...\n");

  int client_fd = -1;
  for (int i = 0; i < 900; ++i) {
    client_fd = accept(listen_fd, NULL, NULL);
    if (client_fd >= 0) {
      break;
    }
    sleep_ms(16);
  }

  if (client_fd < 0) {
    printf(BOLD RED
           "\nNo TCP connection received within timeout period.\n" DEFAULT);
    close(listen_fd);
    return 1;
  }

  printf(BOLD GREEN "\n[TCP Client Connected] fd = %d\n" DEFAULT, client_fd);
  printf("Awaiting data from client...\n");

  char buf[512];
  for (int i = 0; i < 300; ++i) {
    ssize_t rx_bytes = read(client_fd, buf, sizeof(buf) - 1);
    if (rx_bytes > 0) {
      buf[rx_bytes] = '\0';
      printf(BOLD GREEN "[TCP Data Received] %d bytes: \"%s\"\n" DEFAULT,
             (int)rx_bytes, buf);

      printf(BOLD YELLOW "--> Sending TCP Echo Reply back...\n" DEFAULT);
      ssize_t tx_bytes = write(client_fd, buf, (size_t)rx_bytes);
      if (tx_bytes < 0) {
        printf(BOLD RED "Error sending TCP echo reply\n" DEFAULT);
      } else {
        printf(BOLD GREEN "[TCP Echo Transmitted] %d bytes\n" DEFAULT,
               (int)tx_bytes);
      }
      break;
    }
    sleep_ms(16);
  }

  sleep_ms(100);
  close(client_fd);
  close(listen_fd);
  printf(BOLD GREEN "TCP Connection closed cleanly.\n" DEFAULT);
  return 0;
}

int main(int argc, char **argv) {
  if (argc > 1 && (strcmp(argv[1], "tcp") == 0 || strcmp(argv[1], "-t") == 0)) {
    return run_tcp_test();
  }
  return run_udp_test();
}
