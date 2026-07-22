#include "fs.h"

#include "kernel.h"
#include "stdlib.h"
#include "virtio.h"

struct tar_header {
  char name[100];
  char mode[8];
  char uid[8];
  char gid[8];
  char size[12];
  char mtime[12];
  char checksum[8];
  char type;
  char linkname[100];
  char magic[6];
  char version[2];
  char uname[32];
  char gname[32];
  char devmajor[8];
  char devminor[8];
  char prefix[155];
  char padding[12];
  char data[]; // Array pointing to the data area following the header
               // (flexible array member)
} __attribute__((packed));

struct file {
  bool in_use;
  char name[MAX_FILENAME];
  const char *data;
  size_t size;
};

#define FILES_MAX 10
#define DISK_MAX_SECTORS 1024
#define DISK_MAX_SIZE (DISK_MAX_SECTORS * SECTOR_SIZE)

struct file files[FILES_MAX];
uint8_t disk[DISK_MAX_SIZE];

int oct2int(char *oct, int len) {
  int dec = 0;
  for (int i = 0; i < len; i++) {
    if (oct[i] < '0' || oct[i] > '7') {
      break;
    }

    dec = dec * 8 + (oct[i] - '0');
  }
  return dec;
}

void fs_init() {
  uint32_t sectors = virtio_blk_sectors();
  if (sectors > DISK_MAX_SECTORS) {
    sectors = DISK_MAX_SECTORS;
  }

  for (uint32_t sector = 0; sector < sectors; sector++) {
    read_write_device(&disk[sector * SECTOR_SIZE], sector, false);
  }

  uint32_t off = 0;
  for (int i = 0; i < FILES_MAX; ++i) {
    struct tar_header *header = (struct tar_header *)&disk[off];
    if (header->name[0] == '\0') {
      break;
    }

    if (strcmp(header->magic, "ustar") != 0) {
      PANIC("invalid tar header: magic=\"%s\"", header->magic);
    }

    int file_size = oct2int(header->size, sizeof(header->size));
    struct file *file = &files[i];
    file->in_use = true;
    memcpy(file->name, header->name, MAX_FILENAME - 1);
    file->name[MAX_FILENAME - 1] = '\0';
    file->data = header->data;
    file->size = file_size;

    off += align_up(sizeof(struct tar_header) + file_size, SECTOR_SIZE);
  }
}

/*
 * Reads up to FS_CHUNK_SIZE bytes of file contents from the specified offset
 * into a user-provided buffer. Returns the number of bytes actually read on
 * success (0 indicates EOF). Returns -1 if the file is not found.
 */
int fs_read_file(const char *name, char *buf, int offset) {
  for (int i = 0; i < FILES_MAX; i++) {
    if (files[i].in_use && strcmp(files[i].name, name) == 0) {
      if ((size_t)offset >= files[i].size) {
        return 0; // EOF
      }
      size_t read_len = files[i].size - offset;
      if (read_len > FS_CHUNK_SIZE) {
        read_len = FS_CHUNK_SIZE;
      }
      if (buf != NULL) {
        memcpy(buf, files[i].data + offset, read_len);
      }
      return read_len;
    }
  }
  return -1;
}

/*
 * Copies the name of the file at the specified index into a user-provided
 * buffer. Returns 0 on success. Returns -1 if the index is out of bounds or the
 * file slot is unused.
 */
int fs_get_file_name(int index, char *buf, int buf_len) {
  if (index < 0 || index >= FILES_MAX || !files[index].in_use) {
    return -1;
  }
  int i;
  for (i = 0; i < buf_len - 1 && files[index].name[i] != '\0'; i++) {
    buf[i] = files[index].name[i];
  }
  buf[i] = '\0';
  return 0;
}

/*
 * Returns the size of the file at the specified index.
 * Returns -1 if the index is out of bounds or the file slot is unused.
 */
int fs_get_file_size(int index) {
  if (index < 0 || index >= FILES_MAX || !files[index].in_use) {
    return -1;
  }
  return files[index].size;
}

/*
 * Returns a direct pointer to a file's read-only memory area and its size.
 * This is a kernel-internal API to allow process.c to load binary images.
 */
void *fs_get_file_data(const char *name, size_t *size) {
  for (int i = 0; i < FILES_MAX; i++) {
    if (files[i].in_use && strcmp(files[i].name, name) == 0) {
      if (size != NULL) {
        *size = files[i].size;
      }
      return (void *)files[i].data;
    }
  }
  return NULL;
}