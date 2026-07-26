#pragma once

/* Kernel stat structure - crosses the SYSCALL_STAT boundary */
struct k_stat {
  int type;
  int size;
};
