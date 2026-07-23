#pragma once

#ifdef HOST_BUILD
#include <stdint.h>
#include <string.h>
#else
#include "stdlib.h"
#endif

#define BSIZE 1024
#define XV6_FS_MAGIC 0x10203040
#define FS_UNUSED 0
#define FS_DIR 1
#define FS_FILE 2

#define NDIRECT 12
#define NINDIRECT (BSIZE / sizeof(uint32_t)) // 256
#define MAXFILE (NDIRECT + NINDIRECT)

#define NINODE 50
#define SECTORS_PER_BLOCK 2

struct superblock {
  uint32_t magic;      // Must be XV6_FS_MAGIC
  uint32_t size;       // Size of file system image (blocks)
  uint32_t nblocks;    // Number of data blocks
  uint32_t ninodes;    // Number of inodes
  uint32_t inodestart; // Block number of first inode block
  uint32_t bmapstart;  // Block number of first free map block
};

struct dinode {
  uint16_t type;               // File type (FS_UNUSED, FS_DIR, FS_FILE)
  uint16_t major;              // Unused
  uint16_t minor;              // Unused
  uint16_t nlink;              // Number of links to inode
  uint32_t size;               // Size of file (bytes)
  uint32_t addrs[NDIRECT + 1]; // Data block addresses
};

#define MAX_DIR_ENTRIES 64
#define DIRSIZ 30

struct dirent {
  uint16_t inum;
  char name[DIRSIZ];
};

struct stat {
  int type;
  int size;
};

struct inode {
  uint32_t dev;  // Device ID
  uint32_t inum; // Inode number
  int ref;       // Reference count
  int valid;     // Flag: Inode contents loaded from disk?

  uint16_t type; // Copy of dinode properties
  uint16_t major;
  uint16_t minor;
  uint16_t nlink;
  uint32_t size;
  uint32_t addrs[NDIRECT + 1];
};

void fs_init();

int fs_read_file(const char *name, char *buf, int offset);
int fs_write_file(const char *name, const char *buf, int len, int offset);

int fs_get_file_name(int index, char *buf, int buf_len);
int fs_get_file_size(int index);
uint32_t fs_get_inode_size(struct inode *ip);
int fs_stat(const char *path, struct stat *st);
void fs_normalize_path(const char *base, const char *rel, char *dst,
                       size_t dst_len);
int fs_chdir(const char *path, struct inode **pip);
int fs_mkdir(const char *path);

// Inode and read helper declarations
struct inode *iget(uint32_t dev, uint32_t inum);
struct inode *namei(const char *path);
void iput(struct inode *ip);
int readi(struct inode *ip, char *dst, uint32_t offset, uint32_t n);