#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../fs.h"

#define SECTOR_SIZE 512
#define DISK_SIZE_BLOCKS 512 // 512 KB total size

uint8_t disk[DISK_SIZE_BLOCKS * BSIZE];
uint8_t free_bitmap[BSIZE]; // 1 block for bitmap is plenty (8192 bits)

uint32_t freeblock = 7; // Data blocks start at block 7
uint32_t freeinode = 2; // Inode 1 is root directory, files start at inode 2

void mark_allocated(uint32_t block) {
  free_bitmap[block / 8] |= (1 << (block % 8));
}

uint32_t balloc() {
  if (freeblock >= DISK_SIZE_BLOCKS) {
    fprintf(stderr, "error: out of disk blocks\n");
    exit(1);
  }
  uint32_t b = freeblock++;
  mark_allocated(b);
  return b;
}

const char *get_basename(const char *path) {
  const char *base = strrchr(path, '/');
  return base ? base + 1 : path;
}

void write_block(uint32_t block, const void *buf) {
  memcpy(&disk[block * BSIZE], buf, BSIZE);
}

void read_block(uint32_t block, void *buf) {
  memcpy(buf, &disk[block * BSIZE], BSIZE);
}

struct mkfs_dir {
  uint32_t inum;
  char path[256]; // Relative directory path, e.g. "" for root, "cfg" for /cfg
  struct dirent entries[MAX_DIR_ENTRIES];
  int entry_count;
};

struct mkfs_dir dirs[MAX_DIR_ENTRIES];
int num_dirs = 0;

uint32_t get_or_create_dir(const char *dir_path, struct dinode *file_inodes,
                           uint32_t *freeinode) {
  if (strlen(dir_path) == 0) {
    return 1; // Root directory is always inum 1
  }

  // Check if it already exists
  for (int i = 0; i < num_dirs; ++i) {
    if (strcmp(dirs[i].path, dir_path) == 0) {
      return dirs[i].inum;
    }
  }

  if (strlen(dir_path) >= DIRSIZ) {
    fprintf(stderr, "error: directory name '%s' too long (max %d chars)\n",
            dir_path, DIRSIZ - 1);
    exit(1);
  }

  // Does not exist, create it!
  uint32_t parent_inum = 1;
  uint32_t inum = (*freeinode)++;
  if (inum >= MAX_DIR_ENTRIES) {
    fprintf(stderr, "error: out of inodes\n");
    exit(1);
  }

  // Initialize directory inode
  struct dinode *ip = &file_inodes[inum];
  ip->type = FS_DIR;
  ip->nlink = 1;
  ip->size = 0;

  // Setup dirs entry
  struct mkfs_dir *d = &dirs[num_dirs++];
  d->inum = inum;
  strcpy(d->path, dir_path);

  // Add "." and ".." to this new directory
  struct dirent dot;
  dot.inum = inum;
  strcpy(dot.name, ".");
  d->entries[0] = dot;

  struct dirent dotdot;
  dotdot.inum = parent_inum;
  strcpy(dotdot.name, "..");
  d->entries[1] = dotdot;

  d->entry_count = 2;

  // Add this directory entry to the parent directory (dirs[0])
  struct mkfs_dir *parent = &dirs[0];
  if (parent->entry_count >= MAX_DIR_ENTRIES) {
    fprintf(stderr, "error: parent directory full\n");
    exit(1);
  }
  struct dirent de;
  de.inum = inum;
  strcpy(de.name, dir_path);
  parent->entries[parent->entry_count++] = de;

  printf("Created directory '/%s' (inum=%d)\n", dir_path, inum);
  return inum;
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: mkfs <disk.img> [files...]\n");
    exit(1);
  }

  memset(disk, 0, sizeof(disk));
  memset(free_bitmap, 0, sizeof(free_bitmap));

  // Mark metadata blocks as allocated in bitmap
  for (int i = 0; i < 7; ++i) {
    mark_allocated(i);
  }

  // Create superblock
  struct superblock sb;
  sb.magic = XV6_FS_MAGIC;
  sb.size = DISK_SIZE_BLOCKS;
  sb.ninodes = MAX_DIR_ENTRIES;
  sb.inodestart = 2;
  sb.bmapstart = 6;
  sb.nblocks = DISK_SIZE_BLOCKS - 7;

  uint8_t sb_buf[BSIZE];
  memset(sb_buf, 0, BSIZE);
  memcpy(sb_buf, &sb, sizeof(sb));
  write_block(1, sb_buf);

  struct dinode file_inodes[MAX_DIR_ENTRIES];
  memset(file_inodes, 0, sizeof(file_inodes));

  // Set up Root Directory Inode (inum = 1)
  file_inodes[1].type = FS_DIR;
  file_inodes[1].nlink = 1;
  file_inodes[1].size = 0;

  // Initialize root directory in dirs array
  dirs[0].inum = 1;
  strcpy(dirs[0].path, "");

  struct dirent dot;
  dot.inum = 1;
  strcpy(dot.name, ".");
  dirs[0].entries[0] = dot;

  struct dirent dotdot;
  dotdot.inum = 1;
  strcpy(dotdot.name, "..");
  dirs[0].entries[1] = dotdot;

  dirs[0].entry_count = 2;
  num_dirs = 1;

  // Read and add input files
  for (int i = 2; i < argc; ++i) {
    const char *filepath = argv[i];

    // Strip "build/" prefix if present
    const char *rel_path = filepath;
    if (strncmp(filepath, "build/", 6) == 0) {
      rel_path = filepath + 6;
    }

    // Split rel_path into directory and file parts
    char dir_part[256] = "";
    char file_part[256] = "";
    const char *last_slash = strrchr(rel_path, '/');
    if (last_slash != NULL) {
      size_t dir_len = last_slash - rel_path;
      if (dir_len >= sizeof(dir_part)) {
        fprintf(stderr, "error: directory path too long\n");
        exit(1);
      }
      strncpy(dir_part, rel_path, dir_len);
      dir_part[dir_len] = '\0';
      strcpy(file_part, last_slash + 1);
    } else {
      strcpy(file_part, rel_path);
    }

    if (strlen(file_part) >= DIRSIZ) {
      fprintf(stderr, "error: filename '%s' too long (max %d chars)\n",
              file_part, DIRSIZ - 1);
      exit(1);
    }

    FILE *f = fopen(filepath, "rb");
    if (!f) {
      fprintf(stderr, "error: cannot open file %s\n", filepath);
      exit(1);
    }

    // Allocate inode
    uint32_t inum = freeinode++;
    if (inum >= MAX_DIR_ENTRIES) {
      fprintf(stderr, "error: out of inodes\n");
      fclose(f);
      exit(1);
    }

    struct dinode *ip = &file_inodes[inum];
    ip->type = FS_FILE;
    ip->nlink = 1;
    ip->size = 0;

    // Read file contents into blocks
    uint8_t file_buf[BSIZE];
    size_t n;
    uint32_t block_index = 0;
    uint32_t indirect_block = 0;
    uint32_t indirect_addrs[NINDIRECT];
    memset(indirect_addrs, 0, sizeof(indirect_addrs));

    while ((n = fread(file_buf, 1, BSIZE, f)) > 0) {
      uint32_t db = balloc();
      // Write data to disk image block
      write_block(db, file_buf);
      ip->size += n;

      if (block_index < NDIRECT) {
        ip->addrs[block_index] = db;
      } else {
        uint32_t indirect_idx = block_index - NDIRECT;
        if (indirect_idx >= NINDIRECT) {
          fprintf(stderr, "error: file '%s' too large (max %zu bytes)\n",
                  file_part, MAXFILE * BSIZE);
          fclose(f);
          exit(1);
        }
        if (indirect_block == 0) {
          indirect_block = balloc();
          ip->addrs[NDIRECT] = indirect_block;
        }
        indirect_addrs[indirect_idx] = db;
      }
      ++block_index;
      // Reset buffer for clean write (if EOF)
      memset(file_buf, 0, sizeof(file_buf));
    }
    fclose(f);

    // If we used the indirect block, write it back
    if (indirect_block != 0) {
      write_block(indirect_block, indirect_addrs);
    }

    // Get or create directory inode
    uint32_t dir_inum = get_or_create_dir(dir_part, file_inodes, &freeinode);

    // Find directory in dirs array
    int target_dir_idx = -1;
    for (int d = 0; d < num_dirs; ++d) {
      if (dirs[d].inum == dir_inum) {
        target_dir_idx = d;
        break;
      }
    }
    if (target_dir_idx == -1) {
      fprintf(stderr, "error: target directory not found\n");
      exit(1);
    }
    struct mkfs_dir *td = &dirs[target_dir_idx];
    if (td->entry_count >= MAX_DIR_ENTRIES) {
      fprintf(stderr, "error: directory full\n");
      exit(1);
    }
    struct dirent de;
    de.inum = inum;
    strcpy(de.name, file_part);
    td->entries[td->entry_count++] = de;

    printf("Added file '%s' to '/%s' (inum=%d, size=%d bytes, blocks=%d)\n",
           file_part, td->path, inum, ip->size, block_index);
  }

  // Write all directories' content to blocks
  for (int d = 0; d < num_dirs; ++d) {
    struct mkfs_dir *dir = &dirs[d];
    uint32_t dir_size_bytes = dir->entry_count * sizeof(struct dirent);

    // Update its dinode size
    struct dinode *ip = &file_inodes[dir->inum];
    ip->size = dir_size_bytes;

    uint32_t dir_blocks_needed = (dir_size_bytes + BSIZE - 1) / BSIZE;
    if (dir_blocks_needed > NDIRECT) {
      fprintf(stderr, "error: directory too large\n");
      exit(1);
    }
    for (uint32_t block_idx = 0; block_idx < dir_blocks_needed; ++block_idx) {
      uint32_t db = balloc();
      uint8_t temp_buf[BSIZE];
      memset(temp_buf, 0, BSIZE);

      uint32_t bytes_to_copy = dir_size_bytes - block_idx * BSIZE;
      if (bytes_to_copy > BSIZE) {
        bytes_to_copy = BSIZE;
      }
      memcpy(temp_buf, ((uint8_t *)dir->entries) + block_idx * BSIZE,
             bytes_to_copy);
      write_block(db, temp_buf);
      ip->addrs[block_idx] = db;
    }
  }

  // Write Inodes to Inode Blocks (Block 2..5)
  struct dinode block_inodes[16];
  for (int block = 2; block <= 5; ++block) {
    memset(block_inodes, 0, sizeof(block_inodes));
    int start_inum = (block - 2) * 16;
    for (int idx = 0; idx < 16; ++idx) {
      int inum = start_inum + idx;
      if (inum >= 1 && inum < MAX_DIR_ENTRIES) {
        block_inodes[idx] = file_inodes[inum];
      }
    }
    write_block(block, block_inodes);
  }

  // Write bitmap block (Block 6)
  write_block(6, free_bitmap);

  // Write the disk memory block array to file
  FILE *out = fopen(argv[1], "wb");
  if (!out) {
    fprintf(stderr, "error: cannot open output file %s\n", argv[1]);
    exit(1);
  }
  fwrite(disk, 1, sizeof(disk), out);
  fclose(out);

  int total_entries = 0;
  for (int d = 0; d < num_dirs; ++d) {
    total_entries += dirs[d].entry_count;
  }
  printf("Created filesystem image '%s' (%d KB total size, total dir entries: "
         "%d)\n",
         argv[1], DISK_SIZE_BLOCKS * BSIZE / 1024, total_entries);
  return 0;
}
