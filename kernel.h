#pragma once

struct sbiret {
  long error;
  long value;
};

#define PANIC(fmt, ...)                                                       \
  do {                                                                        \
    printf("+++ OUT OF CHEESE +++\n");                                        \
    printf("%s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__);            \
    while (1) {}                                                              \
  } while(0)

