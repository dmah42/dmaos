#include "fs.h"

#include "kernel.h"
#include "stdlib.h"
#include "virtio.h"

static struct superblock sb;
static struct inode inode_table[NINODE];

/*
 * Reads a 1024-byte block (2 sectors) from the block device.
 */
static void read_block(uint32_t block, void *buf) {
  read_write_device(buf, block * SECTORS_PER_BLOCK, false);
  read_write_device((int8_t *)buf + SECTOR_SIZE, block * SECTORS_PER_BLOCK + 1,
                    false);
}

/*
 * In-memory inode cache retrieval.
 */
static struct inode *iget(uint32_t inum) {
  struct inode *empty = NULL;
  for (int i = 0; i < NINODE; i++) {
    if (inode_table[i].ref > 0 && inode_table[i].inum == inum) {
      ++inode_table[i].ref;
      return &inode_table[i];
    }
    if (empty == NULL && inode_table[i].ref == 0) {
      empty = &inode_table[i];
    }
  }

  if (empty == NULL) {
    PANIC("iget: failed to allocate in-memory inode cache slot for inum %d",
          inum);
  }

  empty->inum = inum;
  empty->ref = 1;
  empty->valid = 0;
  return empty;
}

/*
 * Releases a reference to an inode.
 */
void iput(struct inode *ip) {
  if (ip == NULL)
    return;
  if (ip->ref <= 0) {
    PANIC("iput: trying to release inode %d with invalid ref count %d",
          ip->inum, ip->ref);
  }
  --ip->ref;
}

/*
 * Locks/reads the inode content from the block device if not yet cached.
 */
static void ilock(struct inode *ip) {
  if (ip == NULL) {
    PANIC("ilock: NULL inode pointer");
  }
  if (ip->ref < 1) {
    PANIC("ilock: inode %d has invalid ref count %d", ip->inum, ip->ref);
  }

  if (ip->valid == 0) {
    uint32_t inodes_per_block = BSIZE / sizeof(struct dinode);
    uint32_t block = sb.inodestart + ip->inum / inodes_per_block;
    uint32_t offset = (ip->inum % inodes_per_block) * sizeof(struct dinode);

    uint8_t buf[BSIZE];
    read_block(block, buf);
    struct dinode *dip = (struct dinode *)(buf + offset);

    ip->type = dip->type;
    ip->major = dip->major;
    ip->minor = dip->minor;
    ip->nlink = dip->nlink;
    ip->size = dip->size;
    memcpy(ip->addrs, dip->addrs, sizeof(ip->addrs));
    ip->valid = 1;
  }
}

/*
 * Maps a file block number to a physical block address on disk,
 * supporting direct blocks and single indirection.
 */
static uint32_t bmap(struct inode *ip, uint32_t bn) {
  if (ip == NULL) {
    PANIC("bmap: invalid ip\n");
  }
  if (bn < NDIRECT) {
    uint32_t addr = ip->addrs[bn];
    if (addr == 0) {
      PANIC("bmap: inode %d direct block %d is 0", ip->inum, bn);
    }
    return addr;
  }

  bn -= NDIRECT;
  if (bn < NINDIRECT) {
    uint32_t indirect_block = ip->addrs[NDIRECT];
    if (indirect_block == 0) {
      PANIC("bmap: inode %d single-indirect block is 0", ip->inum);
    }
    uint32_t indirect[BSIZE / sizeof(uint32_t)];
    read_block(indirect_block, indirect);
    uint32_t addr = indirect[bn];
    if (addr == 0) {
      PANIC("bmap: inode %d indirect block index %d is 0", ip->inum, bn);
    }
    return addr;
  }

  PANIC("bmap: block index %d exceeds max file size (NDIRECT+NINDIRECT)",
        bn + NDIRECT);
}

/*
 * Reads file contents from the specified inode.
 */
int readi(struct inode *ip, char *dst, uint32_t offset, uint32_t n) {
  if (ip == NULL) {
    kprintf("readi: NULL inode pointer\n");
    return -1;
  }
  if (ip->type == FS_UNUSED) {
    kprintf("readi: inode %d is unused\n", ip->inum);
    return -1;
  }
  ilock(ip);

  if (offset > ip->size || offset + n < offset) {
    kprintf(
        "readi: invalid read parameters: offset %d, size %d, file size %d\n",
        offset, n, ip->size);
    return -1;
  }
  if (offset + n > ip->size) {
    n = ip->size - offset;
  }

  uint32_t tot, m;
  uint8_t buf[BSIZE];
  for (tot = 0; tot < n; tot += m, offset += m, dst += m) {
    uint32_t bn = offset / BSIZE;
    uint32_t block = bmap(ip, bn);
    read_block(block, buf);

    uint32_t block_offset = offset % BSIZE;
    m = BSIZE - block_offset;
    if (m > n - tot) {
      m = n - tot;
    }
    memcpy(dst, buf + block_offset, m);
  }
  return n;
}

/*
 * Scans a directory inode for a entry matching name.
 */
static struct inode *dirlookup(struct inode *dp, const char *name,
                               uint32_t *poff) {
  if (dp->type != FS_DIR) {
    kprintf("dirlookup: inode %d is not a directory (type %d)\n", dp->inum,
            dp->type);
    return NULL;
  }
  ilock(dp);

  struct dirent de;
  for (uint32_t off = 0; off < dp->size; off += sizeof(de)) {
    if (readi(dp, (char *)&de, off, sizeof(de)) != sizeof(de)) {
      kprintf("dirlookup: readi failed reading directory entries of inum %d\n",
              dp->inum);
      return NULL;
    }
    if (de.inum == 0) {
      continue;
    }
    if (strncmp(name, de.name, DIRSIZ) == 0) {
      if (poff) {
        *poff = off;
      }
      return iget(de.inum);
    }
  }
  return NULL;
}

/*
 * Helper to skip path delimiters.
 */
static const char *skipto(const char *path, char *name) {
  while (*path == '/') {
    ++path;
  }
  if (*path == '\0') {
    return NULL;
  }
  const char *s = path;
  while (*path != '/' && *path != '\0') {
    ++path;
  }
  int len = path - s;
  if (len >= DIRSIZ) {
    len = DIRSIZ - 1;
  }
  memcpy(name, s, len);
  name[len] = '\0';
  while (*path == '/') {
    ++path;
  }
  return path;
}

/*
 * Resolves a hierarchical path name to an inode.
 */
static struct inode *namex(const char *path, bool parent, char *name) {
  struct inode *ip;
  if (*path == '/') {
    ip = iget(1); // Root directory is inode 1
  } else {
    // TODO: Current directory is not supported; resolve relative paths from
    // root.
    ip = iget(1);
  }

  while ((path = skipto(path, name)) != NULL) {
    ilock(ip);
    if (ip->type != FS_DIR) {
      kprintf(
          "namex: path component '%s' is not a directory (inum %d, type %d)\n",
          name, ip->inum, ip->type);
      iput(ip);
      return NULL;
    }
    if (parent && *path == '\0') {
      return ip;
    }
    struct inode *next = dirlookup(ip, name, NULL);
    if (next == NULL) {
      kprintf("namex: component '%s' not found in directory inum %d\n", name,
              ip->inum);
      iput(ip);
      return NULL;
    }
    iput(ip);
    ip = next;
  }
  if (parent) {
    iput(ip);
    return NULL;
  }
  return ip;
}

/*
 * Resolves path to an inode.
 */
struct inode *namei(const char *path) {
  char name[DIRSIZ];
  return namex(path, false, name);
}

/*
 * Initializes the filesystem by reading the superblock block.
 */
void fs_init() {
  memset(inode_table, 0, sizeof(inode_table));
  uint8_t buf[BSIZE];
  read_block(1, buf);
  memcpy(&sb, buf, sizeof(sb));
  if (sb.magic != XV6_FS_MAGIC) {
    PANIC("fs_init: magic number mismatch. Expected %x, got %x", XV6_FS_MAGIC,
          sb.magic);
  }
}

/*
 * Reads content of named file into user buffer (handles read offset).
 */
int fs_read_file(const char *name, char *buf, int offset) {
  struct inode *ip = namei(name);
  if (ip == NULL) {
    return -1;
  }
  int n = readi(ip, buf, offset, FS_CHUNK_SIZE);
  iput(ip);
  return n;
}

/*
 * Populates file name at index from root directory (ls compatibility).
 */
int fs_get_file_name(int index, char *buf, int buf_len) {
  if (buf_len <= 0) {
    return -1;
  }
  struct inode *dp = iget(1);
  if (dp == NULL) {
    return -1;
  }
  ilock(dp);

  struct dirent de;
  int current_index = 0;
  for (uint32_t off = 0; off < dp->size; off += sizeof(de)) {
    if (readi(dp, (char *)&de, off, sizeof(de)) != sizeof(de)) {
      iput(dp);
      return -1;
    }
    if (de.inum == 0) {
      continue;
    }
    if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0) {
      continue;
    }

    if (current_index == index) {
      int len = strlen(de.name);
      if (len >= buf_len) {
        len = buf_len - 1;
      }
      memcpy(buf, de.name, len);
      buf[len] = '\0';
      iput(dp);
      return 0;
    }
    ++current_index;
  }

  iput(dp);
  return -1;
}

/*
 * Gets size of file at index from root directory (ls compatibility).
 */
int fs_get_file_size(int index) {
  struct inode *dp = iget(1);
  if (dp == NULL) {
    return -1;
  }
  ilock(dp);

  struct dirent de;
  int current_index = 0;
  for (uint32_t off = 0; off < dp->size; off += sizeof(de)) {
    if (readi(dp, (char *)&de, off, sizeof(de)) != sizeof(de)) {
      iput(dp);
      return -1;
    }
    if (de.inum == 0) {
      continue;
    }
    if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0) {
      continue;
    }

    if (current_index == index) {
      struct inode *ip = iget(de.inum);
      ilock(ip);
      int size = ip->size;
      iput(ip);
      iput(dp);
      return size;
    }
    ++current_index;
  }

  iput(dp);
  return -1;
}

/*
 * Returns the size of the inode, ensuring it is locked/read first.
 */
uint32_t fs_get_inode_size(struct inode *ip) {
  if (ip == NULL) {
    PANIC("Attempt to get size of NULL inode\n");
    return 0;
  }
  ilock(ip);
  return ip->size;
}

/*
 * Populates a stat structure for a path.
 */
int fs_stat(const char *path, struct stat *st) {
  struct inode *ip = namei(path);
  if (ip == NULL) {
    return -1;
  }
  ilock(ip);
  st->type = ip->type;
  st->size = ip->size;
  iput(ip);
  return 0;
}
