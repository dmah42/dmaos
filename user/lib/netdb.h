#pragma once

#include <netinet/in.h>
#include <stdint.h>

struct hostent {
  char *h_name;
  char **h_aliases;
  int h_addrtype;
  int h_length;
  char **h_addr_list;
};

#define h_addr h_addr_list[0]

#define HOST_NOT_FOUND 1
#define TRY_AGAIN 2
#define NO_RECOVERY 3
#define NO_DATA 4

extern int h_errno;

struct hostent *gethostbyname(const char *name);
