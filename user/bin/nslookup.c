#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>

#include "colors.h"

int main(int argc, char **argv) {
  if (argc < 2 || strcmp(argv[1], "--help") == 0 ||
      strcmp(argv[1], "-h") == 0) {
    printf("Usage: nslookup <hostname>\n");
    return 1;
  }

  const char *hostname = argv[1];
  printf("Resolving %s...\n", hostname);

  struct hostent *he = gethostbyname(hostname);
  if (!he) {
    printf(BOLD RED "nslookup: failed to resolve '%s' (h_errno=%d)\n" DEFAULT,
           hostname, h_errno);
    return 1;
  }

  struct in_addr addr;
  memcpy(&addr, he->h_addr_list[0], sizeof(addr));

  printf(BOLD GREEN "Name:    %s\n" DEFAULT, he->h_name);
  printf(BOLD GREEN "Address: %s\n" DEFAULT, inet_ntoa(addr));

  return 0;
}
