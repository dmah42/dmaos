#pragma once

#include "file.h"
#include "fs_shared.h"
#include "k_stat.h"
#include "stdlib.h"

struct inode {
  uint32_t dev;  // Device ID
  uint32_t inum; // Inode number
  int ref;       // Reference count
  int valid;     // Flag: Inode contents loaded from disk?

  struct dinode dinode;
};

void fs_init();

int fs_create(const char *path, int flags);
int fs_open(const char *path, int flags);
int fs_read(int fd, char *buf, int n);
int fs_write(int fd, const char *buf, int n);
int fs_close(int fd);
int fs_ftruncate(int fd, int length);
int fs_lseek(int fd, int offset, int whence);
int fs_rm(const char *path);

int fs_get_file_name(int index, char *buf, int buf_len);
int fs_get_file_size(int index);
uint32_t fs_get_inode_size(struct inode *ip);
int fs_stat(const char *path, struct k_stat *st);
void fs_normalize_path(const char *base, const char *rel, char *dst,
                       size_t dst_len);
int fs_chdir(const char *path, struct inode **pip);
int fs_mkdir(const char *path);

// Inode and read helper declarations
struct inode *iget(uint32_t dev, uint32_t inum);
struct inode *namei(const char *path);
void iput(struct inode *ip);
int readi(struct inode *ip, char *dst, uint32_t offset, uint32_t n);