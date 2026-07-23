#include "fs.h"

#include "errno.h"
#include "kernel.h"
#include "process.h"
#include "stdlib.h"
#include "virtio.h"

static struct superblock sb[2];
static struct inode inode_table[NINODE];

struct mount {
  uint32_t parent_dev;
  uint32_t parent_inum;
  uint32_t child_dev;
};

static struct mount mounts[4];
static int num_mounts = 0;

/*
 * Reads a 1024-byte block (2 sectors) from the block device.
 */
static void read_block(uint32_t dev, uint32_t block, void *buf) {
  read_device(dev, buf, block * SECTORS_PER_BLOCK);
  read_device(dev, (int8_t *)buf + SECTOR_SIZE, block * SECTORS_PER_BLOCK + 1);
}

/*
 * In-memory inode cache retrieval.
 */
struct inode *iget(uint32_t dev, uint32_t inum) {
  struct inode *empty = NULL;
  for (int i = 0; i < NINODE; ++i) {
    if (inode_table[i].ref > 0 && inode_table[i].dev == dev &&
        inode_table[i].inum == inum) {
      ++inode_table[i].ref;
      return &inode_table[i];
    }
    if (empty == NULL && inode_table[i].ref == 0) {
      empty = &inode_table[i];
    }
  }

  if (empty == NULL) {
    PANIC("iget: failed to allocate in-memory inode cache slot for dev %d, "
          "inum %d",
          dev, inum);
  }

  empty->dev = dev;
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
    uint32_t block = sb[ip->dev].inodestart + ip->inum / inodes_per_block;
    uint32_t offset = (ip->inum % inodes_per_block) * sizeof(struct dinode);

    uint8_t buf[BSIZE];
    read_block(ip->dev, block, buf);
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
    read_block(ip->dev, indirect_block, indirect);
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
  ilock(ip);
  if (ip->type == FS_UNUSED) {
    kprintf("readi: inode %d is unused\n", ip->inum);
    return -1;
  }

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
    read_block(ip->dev, block, buf);

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
      return iget(dp->dev, de.inum);
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
    ip = iget(0, 1); // Root directory is inode 1 on Device 0
  } else {
    struct Process *curr = get_current_process();
    if (curr != NULL && curr->cwd != NULL) {
      ip = iget(curr->cwd->dev, curr->cwd->inum);
    } else {
      ip = iget(0, 1);
    }
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

    struct inode *next = NULL;

    // Upward traversal: if looking up ".." and we are at the root of a mounted
    // device
    if (strcmp(name, "..") == 0 && ip->inum == 1 && ip->dev != 0) {
      for (int i = 0; i < num_mounts; ++i) {
        if (mounts[i].child_dev == ip->dev) {
          struct inode *parent_mount =
              iget(mounts[i].parent_dev, mounts[i].parent_inum);
          ilock(parent_mount);
          next = dirlookup(parent_mount, "..", NULL);
          iput(parent_mount);
          break;
        }
      }
    }

    if (next == NULL) {
      next = dirlookup(ip, name, NULL);
    }

    if (next == NULL) {
      kprintf("namex: component '%s' not found in directory inum %d\n", name,
              ip->inum);
      iput(ip);
      return NULL;
    }

    // Downward traversal: if next matches a registered mountpoint
    for (int i = 0; i < num_mounts; ++i) {
      if (mounts[i].parent_dev == next->dev &&
          mounts[i].parent_inum == next->inum) {
        iput(next);
        next =
            iget(mounts[i].child_dev, 1); // Switch to root of the child device
        break;
      }
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

  // Initialize Device 0
  read_block(0, 1, buf);
  memcpy(&sb[0], buf, sizeof(sb[0]));
  if (sb[0].magic != XV6_FS_MAGIC) {
    PANIC("fs_init: Device 0 magic mismatch");
  }

  // Initialize Device 1
  read_block(1, 1, buf);
  memcpy(&sb[1], buf, sizeof(sb[1]));
  if (sb[1].magic != XV6_FS_MAGIC) {
    PANIC("fs_init: Device 1 magic mismatch");
  }

  // Find `/home` inode on Device 0
  struct inode *ip_home = namei("/home");
  if (ip_home == NULL) {
    PANIC("fs_init: mountpoint /home not found on Device 0");
  }

  // Mount Device 1 at `/home`
  mounts[0].parent_dev = ip_home->dev;
  mounts[0].parent_inum = ip_home->inum;
  mounts[0].child_dev = 1;
  num_mounts = 1;

  iput(ip_home);
}

/*
 * Reads content of named file into user buffer (handles read offset).
 */
int fs_read_file(const char *name, char *buf, int offset) {
  struct inode *ip = namei(name);
  if (ip == NULL) {
    return ERR_NOT_FOUND;
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
  struct inode *dp = iget(0, 1);
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
  struct inode *dp = iget(0, 1);
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
      struct inode *ip = iget(0, de.inum);
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
    return ERR_NOT_FOUND;
  }
  ilock(ip);
  st->type = ip->type;
  st->size = ip->size;
  iput(ip);
  return 0;
}

int fs_chdir(const char *path, struct inode **pip) {
  struct inode *ip = namei(path);
  if (ip == NULL) {
    return ERR_NOT_FOUND;
  }
  ilock(ip);
  if (ip->type != FS_DIR) {
    iput(ip);
    return ERR_NOT_A_DIRECTORY;
  }
  *pip = ip;
  return 0;
}

void fs_normalize_path(const char *base, const char *rel, char *dst,
                       size_t dst_len) {
  char buf[256];
  if (rel[0] == '/') {
    strncpy(buf, rel, sizeof(buf) - 1);
  } else {
    strncpy(buf, base, sizeof(buf) - 1);
    int len = strlen(buf);
    if (len > 0 && buf[len - 1] != '/') {
      strncat(buf, "/", sizeof(buf) - len - 1);
    }
    strncat(buf, rel, sizeof(buf) - strlen(buf) - 1);
  }
  buf[sizeof(buf) - 1] = '\0';

  char tokens[MAX_PATH_DEPTH][MAX_FILENAME];
  int num_tokens = 0;
  char *p = buf;
  while (*p == '/') {
    ++p;
  }

  while (*p) {
    char *next = p;
    while (*next && *next != '/') {
      ++next;
    }
    char orig = *next;
    *next = '\0';

    if (strcmp(p, ".") == 0) {
      // skip
    } else if (strcmp(p, "..") == 0) {
      if (num_tokens > 0) {
        --num_tokens;
      }
    } else if (strlen(p) > 0) {
      if (num_tokens < MAX_PATH_DEPTH) {
        strncpy(tokens[num_tokens], p, sizeof(tokens[num_tokens]) - 1);
        tokens[num_tokens][sizeof(tokens[num_tokens]) - 1] = '\0';
        ++num_tokens;
      }
    }

    if (orig) {
      *next = orig;
      p = next + 1;
      while (*p == '/') {
        ++p;
      }
    } else {
      break;
    }
  }

  dst[0] = '/';
  dst[1] = '\0';
  for (int i = 0; i < num_tokens; ++i) {
    if (i > 0 || dst[1] != '\0') {
      strncat(dst, "/", dst_len - strlen(dst) - 1);
    }
    strncat(dst, tokens[i], dst_len - strlen(dst) - 1);
  }
}

static void write_block(uint32_t dev, uint32_t block, const void *buf) {
  write_device(dev, (void *)buf, block * SECTORS_PER_BLOCK);
  write_device(dev, (void *)((int8_t *)buf + SECTOR_SIZE),
               block * SECTORS_PER_BLOCK + 1);
}

void iupdate(struct inode *ip) {
  uint32_t inodes_per_block = BSIZE / sizeof(struct dinode);
  uint32_t block = sb[ip->dev].inodestart + ip->inum / inodes_per_block;
  uint32_t offset = (ip->inum % inodes_per_block) * sizeof(struct dinode);

  uint8_t buf[BSIZE];
  read_block(ip->dev, block, buf);
  struct dinode *dip = (struct dinode *)(buf + offset);
  dip->type = ip->type;
  dip->major = ip->major;
  dip->minor = ip->minor;
  dip->nlink = ip->nlink;
  dip->size = ip->size;
  memcpy(dip->addrs, ip->addrs, sizeof(ip->addrs));
  write_block(ip->dev, block, buf);
}

static uint32_t balloc(uint32_t dev) {
  uint8_t buf[BSIZE];
  read_block(dev, sb[dev].bmapstart, buf);
  for (uint32_t b = 0; b < sb[dev].nblocks; ++b) {
    int idx = b / 8;
    int bit = b % 8;
    if ((buf[idx] & (1 << bit)) == 0) {
      buf[idx] |= (1 << bit);
      write_block(dev, sb[dev].bmapstart, buf);

      uint8_t zero[BSIZE];
      memset(zero, 0, sizeof(zero));
      write_block(dev, b, zero);
      return b;
    }
  }
  PANIC("balloc: out of blocks");
  return 0;
}

struct inode *ialloc(uint32_t dev, uint32_t type) {
  uint32_t inodes_per_block = BSIZE / sizeof(struct dinode);
  uint8_t buf[BSIZE];
  for (uint32_t inum = 1; inum < sb[dev].ninodes; ++inum) {
    uint32_t block = sb[dev].inodestart + inum / inodes_per_block;
    uint32_t offset = (inum % inodes_per_block) * sizeof(struct dinode);
    read_block(dev, block, buf);
    struct dinode *dip = (struct dinode *)(buf + offset);
    if (dip->type == FS_UNUSED) {
      memset(dip, 0, sizeof(*dip));
      dip->type = type;
      dip->nlink = 1;
      write_block(dev, block, buf);

      struct inode *ip = iget(dev, inum);
      if (ip != NULL) {
        ip->type = type;
        ip->major = 0;
        ip->minor = 0;
        ip->nlink = 1;
        ip->size = 0;
        memset(ip->addrs, 0, sizeof(ip->addrs));
        ip->valid = 1;
      }
      return ip;
    }
  }
  return NULL;
}

static uint32_t bmap_alloc(struct inode *ip, uint32_t bn) {
  if (ip == NULL) {
    PANIC("bmap_alloc: invalid ip\n");
  }
  if (bn < NDIRECT) {
    uint32_t addr = ip->addrs[bn];
    if (addr == 0) {
      addr = balloc(ip->dev);
      ip->addrs[bn] = addr;
      iupdate(ip);
    }
    return addr;
  }

  bn -= NDIRECT;
  if (bn < NINDIRECT) {
    uint32_t indirect_block = ip->addrs[NDIRECT];
    if (indirect_block == 0) {
      indirect_block = balloc(ip->dev);
      ip->addrs[NDIRECT] = indirect_block;
      iupdate(ip);
    }
    uint32_t indirect[BSIZE / sizeof(uint32_t)];
    read_block(ip->dev, indirect_block, indirect);
    uint32_t addr = indirect[bn];
    if (addr == 0) {
      addr = balloc(ip->dev);
      indirect[bn] = addr;
      write_block(ip->dev, indirect_block, indirect);
    }
    return addr;
  }
  PANIC("bmap_alloc: out of range");
  return 0;
}

int writei(struct inode *ip, const char *src, uint32_t offset, uint32_t n) {
  if (ip == NULL) {
    kprintf("write: attempt to write to null ip");
    return -1;
  }
  ilock(ip);
  if (offset > ip->size || offset + n < offset) {
    kprintf("write: invalid offset or n");
    return -1;
  }
  if (offset + n > MAXFILE * BSIZE) {
    kprintf("write: file too large (%d)", offset + n);
    return -1;
  }

  uint32_t tot, m;
  uint8_t buf[BSIZE];
  for (tot = 0; tot < n; tot += m, offset += m, src += m) {
    uint32_t bn = offset / BSIZE;
    uint32_t block = bmap_alloc(ip, bn);
    read_block(ip->dev, block, buf);

    uint32_t block_offset = offset % BSIZE;
    m = BSIZE - block_offset;
    if (m > n - tot) {
      m = n - tot;
    }
    memcpy(buf + block_offset, src, m);
    write_block(ip->dev, block, buf);
  }

  if (n > 0 && offset > ip->size) {
    ip->size = offset;
    iupdate(ip);
  }
  return n;
}

static void bfree(uint32_t dev, uint32_t block) {
  uint8_t buf[BSIZE];
  read_block(dev, sb[dev].bmapstart, buf);
  int idx = block / 8;
  int bit = block % 8;
  if ((buf[idx] & (1 << bit)) == 0) {
    PANIC("bfree: freeing free block %d\n", block);
  }
  buf[idx] &= ~(1 << bit);
  write_block(dev, sb[dev].bmapstart, buf);
}

static void itrunc(struct inode *ip, uint32_t new_size) {
  ilock(ip);
  if (new_size >= ip->size) {
    return;
  }

  uint32_t old_blocks = (ip->size + BSIZE - 1) / BSIZE;
  uint32_t new_blocks = (new_size + BSIZE - 1) / BSIZE;

  if (new_blocks < old_blocks) {
    for (uint32_t bn = new_blocks; bn < old_blocks; ++bn) {
      if (bn < NDIRECT) {
        uint32_t addr = ip->addrs[bn];
        if (addr != 0) {
          bfree(ip->dev, addr);
          ip->addrs[bn] = 0;
        }
      } else {
        uint32_t indirect_idx = bn - NDIRECT;
        uint32_t indirect_block = ip->addrs[NDIRECT];
        if (indirect_block != 0) {
          uint32_t indirect[BSIZE / sizeof(uint32_t)];
          read_block(ip->dev, indirect_block, indirect);
          uint32_t addr = indirect[indirect_idx];
          if (addr != 0) {
            bfree(ip->dev, addr);
            indirect[indirect_idx] = 0;
            write_block(ip->dev, indirect_block, indirect);
          }
        }
      }
    }

    if (new_blocks <= NDIRECT) {
      uint32_t indirect_block = ip->addrs[NDIRECT];
      if (indirect_block != 0) {
        bfree(ip->dev, indirect_block);
        ip->addrs[NDIRECT] = 0;
      }
    }
  }

  ip->size = new_size;
  iupdate(ip);
}

int dirlink(struct inode *dp, const char *name, uint32_t inum) {
  struct inode *ip = dirlookup(dp, name, NULL);
  if (ip != NULL) {
    iput(ip);
    kprintf("dirlink: file '%s' already exists\n", name);
    return -1;
  }

  ilock(dp);
  struct dirent de;
  uint32_t off;
  for (off = 0; off < dp->size; off += sizeof(de)) {
    if (readi(dp, (char *)&de, off, sizeof(de)) != sizeof(de)) {
      return -1;
    }
    if (de.inum == 0) {
      break;
    }
  }

  de.inum = inum;
  strncpy(de.name, name, DIRSIZ);
  de.name[DIRSIZ - 1] = '\0';
  if (writei(dp, (char *)&de, off, sizeof(de)) != sizeof(de)) {
    return -1;
  }
  return 0;
}

int fs_write_file(const char *name, const char *buf, int len, int offset) {
  char file_part[256];
  struct inode *dp = namex(name, true, file_part);
  if (dp == NULL) {
    kprintf("fs_write_file: attempt to write to null inode\n");
    return ERR_NOT_FOUND;
  }

  if (dp->dev == 0) {
    kprintf("fs_write_file: write access denied on dev %d (read-only)\n",
            dp->dev);
    iput(dp);
    return ERR_PERMISSION_DENIED;
  }

  struct inode *ip = dirlookup(dp, file_part, NULL);
  if (ip == NULL) {
    if (offset != 0) {
      iput(dp);
      return ERR_NOT_FOUND;
    }
    ip = ialloc(dp->dev, FS_FILE);
    if (ip == NULL) {
      iput(dp);
      return ERR_NO_SPACE;
    }
    if (dirlink(dp, file_part, ip->inum) < 0) {
      iput(ip);
      iput(dp);
      return ERR_NO_SPACE;
    }
  } else if (offset == 0) {
    itrunc(ip, 0);
  }

  int n = writei(ip, buf, offset, len);
  iput(ip);
  iput(dp);
  return n < 0 ? ERR_NO_SPACE : n;
}

int fs_mkdir(const char *path) {
  char file_part[256];
  struct inode *dp = namex(path, true, file_part);
  if (dp == NULL) {
    kprintf("fs_mkdir: attempt to mkdir in non-existent parent directory\n");
    return ERR_NOT_FOUND;
  }

  if (dp->dev == 0) {
    kprintf("fs_mkdir: write access denied on dev %d (read-only)\n", dp->dev);
    iput(dp);
    return ERR_PERMISSION_DENIED;
  }

  struct inode *ip = dirlookup(dp, file_part, NULL);
  if (ip != NULL) {
    iput(ip);
    iput(dp);
    return ERR_ALREADY_EXISTS;
  }

  ip = ialloc(dp->dev, FS_DIR);
  if (ip == NULL) {
    iput(dp);
    return ERR_NO_SPACE;
  }

  struct dirent dot;
  memset(&dot, 0, sizeof(dot));
  dot.inum = ip->inum;
  strncpy(dot.name, ".", sizeof(dot.name) - 1);
  if (writei(ip, (char *)&dot, 0, sizeof(dot)) != sizeof(dot)) {
    iput(ip);
    iput(dp);
    return ERR_NO_SPACE;
  }

  struct dirent dotdot;
  memset(&dotdot, 0, sizeof(dotdot));
  dotdot.inum = dp->inum;
  strncpy(dotdot.name, "..", sizeof(dotdot.name) - 1);
  if (writei(ip, (char *)&dotdot, sizeof(dot), sizeof(dotdot)) !=
      sizeof(dotdot)) {
    iput(ip);
    iput(dp);
    return ERR_NO_SPACE;
  }

  if (dirlink(dp, file_part, ip->inum) < 0) {
    iput(ip);
    iput(dp);
    return ERR_NO_SPACE;
  }

  iput(ip);
  iput(dp);
  return 0;
}

static bool is_dir_empty(struct inode *ip) {
  struct dirent de;
  for (uint32_t off = 0; off < ip->size; off += sizeof(de)) {
    if (readi(ip, (char *)&de, off, sizeof(de)) != sizeof(de)) {
      return false;
    }
    if (de.inum != 0) {
      char name_buf[DIRSIZ + 1];
      memcpy(name_buf, de.name, DIRSIZ);
      name_buf[DIRSIZ] = '\0';
      if (strcmp(name_buf, ".") != 0 && strcmp(name_buf, "..") != 0) {
        return false;
      }
    }
  }
  return true;
}

int fs_rm(const char *path) {
  char file_part[256];
  struct inode *dp = namex(path, true, file_part);
  if (dp == NULL) {
    kprintf("fs_rm: parent directory not found\n");
    return ERR_NOT_FOUND;
  }

  if (dp->dev == 0) {
    kprintf("fs_rm: write access denied on dev %d (read-only)\n", dp->dev);
    iput(dp);
    return ERR_PERMISSION_DENIED;
  }

  uint32_t off = 0;
  struct inode *ip = dirlookup(dp, file_part, &off);
  if (ip == NULL) {
    kprintf("fs_rm: file '%s' not found\n", file_part);
    iput(dp);
    return ERR_NOT_FOUND;
  }

  ilock(ip);
  if (ip->type == FS_DIR) {
    if (!is_dir_empty(ip)) {
      kprintf("fs_rm: directory '%s' is not empty\n", file_part);
      iput(ip);
      iput(dp);
      return ERR_DIRECTORY_NOT_EMPTY;
    }
  }

  struct dirent de;
  memset(&de, 0, sizeof(de));
  if (writei(dp, (char *)&de, off, sizeof(de)) != sizeof(de)) {
    kprintf("fs_rm: failed to clear directory entry\n");
    iput(ip);
    iput(dp);
    return ERR_IO;
  }

  itrunc(ip, 0);

  ip->type = FS_UNUSED;
  iupdate(ip);

  iput(ip);
  iput(dp);
  return 0;
}
