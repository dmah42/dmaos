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
  char name[100];
  char data[1024];
  size_t size;
};

#define FILES_MAX 3
#define DISK_MAX_SIZE align_up(sizeof(struct file) * FILES_MAX, SECTOR_SIZE)

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
  for (uint32_t sector = 0; sector < sizeof(disk) / SECTOR_SIZE; sector++) {
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
    // TODO: strncpy
    memcpy(file->name, header->name, 99);
    memcpy(file->data, header->data, file_size);
    file->size = file_size;
    printf("file: %s, size=%d\n", file->name, file->size);

    off += align_up(sizeof(struct tar_header) + file_size, SECTOR_SIZE);
  }
}