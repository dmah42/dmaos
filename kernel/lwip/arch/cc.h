#pragma once

#include <stdint.h>
#include "kernel.h"
#include "stdlib.h"

// Freestanding environment configuration
#define LWIP_NO_INTTYPES_H 1
#define U16_F "u"
#define S16_F "d"
#define X16_F "x"
#define U32_F "u"
#define S32_F "d"
#define X32_F "x"
#define SZT_F "u"

#define LWIP_NO_UNISTD_H 1
#define LWIP_NO_CTYPE_H 1

// Packing attribute
#define PACK_STRUCT_STRUCT _packed
#define PACK_STRUCT_FIELD(x) x

// Platform diagnostics and panic handlers
#define LWIP_PLATFORM_DIAG(x)   do { kprintf x; } while (0)
#define LWIP_PLATFORM_ASSERT(x) do { PANIC("lwIP assertion failed: %s", x); } while (0)

#define LWIP_RAND() rand32()

typedef int sys_prot_t;
