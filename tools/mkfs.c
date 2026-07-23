#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Temporarily alias guest structs during inclusion to avoid clashing with host
// standard headers
#define dirent fsdirent
#define stat fsstat
#include "../fs.h"
#undef dirent
#undef stat

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

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

void write_block(uint32_t block, const void *buf) {
  memcpy(&disk[block * BSIZE], buf, BSIZE);
}

struct mkfs_dir {
  uint32_t inum;
  char path[256]; // Relative directory path, e.g. "" for root, "cfg" for /cfg
  struct fsdirent entries[MAX_DIR_ENTRIES];
  int entry_count;
};

struct mkfs_dir dirs[MAX_DIR_ENTRIES];
int num_dirs = 0;

uint32_t get_or_create_dir(const char *dir_path, struct dinode *file_inodes,
                           uint32_t *freeinode_ptr) {
  if (strlen(dir_path) == 0) {
    return 1; // Root directory is always inum 1
  }

  // Check if it already exists
  for (int i = 0; i < num_dirs; ++i) {
    if (strcmp(dirs[i].path, dir_path) == 0) {
      return dirs[i].inum;
    }
  }

  // Split into parent path and this directory's name
  char parent_path[256] = "";
  char dir_name[256] = "";
  const char *last_slash = strrchr(dir_path, '/');
  if (last_slash != NULL) {
    size_t parent_len = last_slash - dir_path;
    strncpy(parent_path, dir_path, parent_len);
    parent_path[parent_len] = '\0';
    strcpy(dir_name, last_slash + 1);
  } else {
    strcpy(dir_name, dir_path);
  }

  if (strlen(dir_name) >= DIRSIZ) {
    fprintf(stderr, "error: directory name '%s' too long (max %d chars)\n",
            dir_name, DIRSIZ - 1);
    exit(1);
  }

  // Get or create parent directory
  uint32_t parent_inum =
      get_or_create_dir(parent_path, file_inodes, freeinode_ptr);

  // Allocate inode
  uint32_t inum = (*freeinode_ptr)++;
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
  struct fsdirent dot;
  dot.inum = inum;
  strcpy(dot.name, ".");
  d->entries[0] = dot;

  struct fsdirent dotdot;
  dotdot.inum = parent_inum;
  strcpy(dotdot.name, "..");
  d->entries[1] = dotdot;

  d->entry_count = 2;

  // Find parent directory in dirs array and add this entry to it
  int parent_idx = -1;
  for (int i = 0; i < num_dirs; ++i) {
    if (dirs[i].inum == parent_inum) {
      parent_idx = i;
      break;
    }
  }
  if (parent_idx == -1) {
    fprintf(stderr, "error: parent directory not found in dirs array\n");
    exit(1);
  }
  struct mkfs_dir *p_dir = &dirs[parent_idx];
  if (p_dir->entry_count >= MAX_DIR_ENTRIES) {
    fprintf(stderr, "error: directory full\n");
    exit(1);
  }
  struct fsdirent de;
  de.inum = inum;
  strcpy(de.name, dir_name);
  p_dir->entries[p_dir->entry_count++] = de;

  printf("Created directory '/%s' (inum=%d)\n", dir_path, inum);
  return inum;
}

void add_file(const char *host_path, const char *fs_path,
              struct dinode *file_inodes, uint32_t *freeinode_ptr) {
  // Split fs_path into directory and file parts
  char dir_part[256] = "";
  char file_part[256] = "";
  const char *last_slash = strrchr(fs_path, '/');
  if (last_slash != NULL) {
    size_t dir_len = last_slash - fs_path;
    if (dir_len >= sizeof(dir_part)) {
      fprintf(stderr, "error: directory path too long\n");
      exit(1);
    }
    strncpy(dir_part, fs_path, dir_len);
    dir_part[dir_len] = '\0';
    strcpy(file_part, last_slash + 1);
  } else {
    strcpy(file_part, fs_path);
  }

  if (strlen(file_part) >= DIRSIZ) {
    fprintf(stderr, "error: filename '%s' too long (max %d chars)\n", file_part,
            DIRSIZ - 1);
    exit(1);
  }

  FILE *f = fopen(host_path, "rb");
  if (!f) {
    fprintf(stderr, "error: cannot open file %s\n", host_path);
    exit(1);
  }

  // Allocate inode
  uint32_t inum = (*freeinode_ptr)++;
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
  uint32_t dir_inum = get_or_create_dir(dir_part, file_inodes, freeinode_ptr);

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
  struct fsdirent de;
  de.inum = inum;
  strcpy(de.name, file_part);
  td->entries[td->entry_count++] = de;

  printf("Added file '%s' to '/%s' (inum=%d, size=%d bytes, blocks=%d)\n",
         file_part, td->path, inum, ip->size, block_index);
}

void traverse_dir(const char *host_path, const char *fs_path,
                  struct dinode *file_inodes, uint32_t *freeinode_ptr) {
  DIR *dir = opendir(host_path);
  if (!dir) {
    fprintf(stderr, "error: could not open host directory %s\n", host_path);
    exit(1);
  }

  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    char next_host[512];
    snprintf(next_host, sizeof(next_host), "%s/%s", host_path, entry->d_name);

    char next_fs[256];
    if (strlen(fs_path) == 0) {
      snprintf(next_fs, sizeof(next_fs), "%s", entry->d_name);
    } else {
      snprintf(next_fs, sizeof(next_fs), "%s/%s", fs_path, entry->d_name);
    }

    struct stat st;
    if (stat(next_host, &st) < 0) {
      fprintf(stderr, "error: stat failed for %s\n", next_host);
      exit(1);
    }

    if (S_ISDIR(st.st_mode)) {
      get_or_create_dir(next_fs, file_inodes, freeinode_ptr);
      traverse_dir(next_host, next_fs, file_inodes, freeinode_ptr);
    } else if (S_ISREG(st.st_mode)) {
      add_file(next_host, next_fs, file_inodes, freeinode_ptr);
    }
  }
  closedir(dir);
}

int main(int argc, char *argv[]) {
  if (argc < 2 || argc > 3) {
    fprintf(stderr, "Usage: mkfs <disk.img> [staging_dir]\n");
    exit(1);
  }

  const char *disk_img = argv[1];
  const char *staging_dir = (argc == 3) ? argv[2] : NULL;

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

  struct fsdirent dot;
  dot.inum = 1;
  strcpy(dot.name, ".");
  dirs[0].entries[0] = dot;

  struct fsdirent dotdot;
  dotdot.inum = 1;
  strcpy(dotdot.name, "..");
  dirs[0].entries[1] = dotdot;

  dirs[0].entry_count = 2;
  num_dirs = 1;

  if (staging_dir != NULL) {
    traverse_dir(staging_dir, "", file_inodes, &freeinode);
  }

  // Write all directories' content to blocks
  for (int d = 0; d < num_dirs; ++d) {
    struct mkfs_dir *dir = &dirs[d];
    uint32_t dir_size_bytes = dir->entry_count * sizeof(struct fsdirent);

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
  FILE *out = fopen(disk_img, "wb");
  if (!out) {
    fprintf(stderr, "error: cannot open output file %s\n", disk_img);
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
         disk_img, DISK_SIZE_BLOCKS * BSIZE / 1024, total_entries);
  return 0;
}
