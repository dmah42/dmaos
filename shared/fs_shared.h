#pragma once

#include <stdint.h>

#define BSIZE 1024

#define XV6_FS_MAGIC 0x10203040

#define NDIRECT 12
#define NINDIRECT (BSIZE / 4) // BSIZE / sizeof(uint32_t) -> 256
#define MAXFILE (NDIRECT + NINDIRECT)

#define MAX_DIR_ENTRIES 64
#define DIRSIZ 30

#define NINODE 50
#define SECTORS_PER_BLOCK 2

enum FileType {
  FT_UNUSED = 0,
  FT_DIRECTORY = 1,
  FT_FILE = 2,
};

struct superblock {
  uint32_t magic;      // Must be XV6_FS_MAGIC
  uint32_t size;       // Size of file system image (blocks)
  uint32_t nblocks;    // Number of data blocks
  uint32_t ninodes;    // Number of inodes
  uint32_t inodestart; // Block number of first inode block
  uint32_t bmapstart;  // Block number of first free map block
};

struct dinode {
  uint16_t type;               // File type (enum FileType)
  uint16_t major;              // Unused
  uint16_t minor;              // Unused
  uint16_t nlink;              // Number of links to inode
  uint32_t size;               // Size of file (bytes)
  uint32_t addrs[NDIRECT + 1]; // Data block addresses
};

struct fsdirent {
  uint16_t inum;
  char name[DIRSIZ];
};

#define O_READ 0x001
#define O_WRITE 0x002
#define O_RDWR (O_READ | O_WRITE)
#define O_CREATE 0x004
#define O_TRUNC 0x008
#define O_APPEND 0x010
