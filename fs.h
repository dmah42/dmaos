#pragma once

#include "file.h"

#include "stdlib.h"

#define BSIZE 1024

#define XV6_FS_MAGIC 0x10203040

#define NDIRECT 12
#define NINDIRECT (BSIZE / sizeof(uint32_t)) // 256
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
  enum FileType type;
  // uint16_t major;              // Unused
  // uint16_t minor;              // Unused
  uint16_t nlink;              // Number of links to inode
  uint32_t size;               // Size of file (bytes)
  uint32_t addrs[NDIRECT + 1]; // Data block addresses
};

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

  struct dinode dinode;
};

#define O_READ 0x001
#define O_WRITE 0x002
#define O_RDWR (O_READ | O_WRITE)
#define O_CREATE 0x004
#define O_TRUNC 0x008
#define O_APPEND 0x010

void fs_init();

int fs_create(const char *path, int flags);
int fs_open(const char *path, int flags);
int fs_read(int fd, char *buf, int n);
int fs_write(int fd, const char *buf, int n);
int fs_close(int fd);
int fs_rm(const char *path);

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