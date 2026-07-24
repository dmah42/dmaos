#include "virtio.h"

#include "kernel.h"
#include "page.h"
#include "stdlib.h"

#define VIRTQ_ENTRY_NUM 16
#define VIRTIO_DEVICE_BLK 2
#define VIRTIO_REG_MAGIC 0x00
#define VIRTIO_REG_VERSION 0x04
#define VIRTIO_REG_DEVICE_ID 0x08
#define VIRTIO_REG_QUEUE_SEL 0x30
#define VIRTIO_REG_QUEUE_NUM_MAX 0x34
#define VIRTIO_REG_QUEUE_NUM 0x38
#define VIRTIO_REG_QUEUE_ALIGN 0x3c
#define VIRTIO_REG_QUEUE_PFN 0x40
#define VIRTIO_REG_QUEUE_READY 0x44
#define VIRTIO_REG_QUEUE_NOTIFY 0x50
#define VIRTIO_REG_DEVICE_STATUS 0x70
#define VIRTIO_REG_DEVICE_CONFIG 0x100

#define VIRTIO_STATUS_ACK 1
#define VIRTIO_STATUS_DRIVER 2
#define VIRTIO_STATUS_DRIVER_OK 4
#define VIRTIO_STATUS_FEAT_OK 8

#define VIRTQ_DESC_F_NEXT 1
#define VIRTQ_DESC_F_WRITE 2

#define VIRTQ_AVAIL_F_NO_INTERRUPT 1

#define VIRTIO_BLK_T_IN 0
#define VIRTIO_BLK_T_OUT 1

// Virtqueue Descriptor area entry.
struct virtq_desc {
  uint64_t addr;
  uint32_t len;
  uint16_t flags;
  uint16_t next;
} __attribute__((packed));

// Virtqueue Available Ring.
struct virtq_avail {
  uint16_t flags;
  uint16_t index;
  uint16_t ring[VIRTQ_ENTRY_NUM];
} __attribute__((packed));

// Virtqueue Used Ring entry.
struct virtq_used_elem {
  uint32_t id;
  uint32_t len;
} __attribute__((packed));

// Virtqueue Used Ring.
struct virtq_used {
  uint16_t flags;
  uint16_t index;
  struct virtq_used_elem ring[VIRTQ_ENTRY_NUM];
} __attribute__((packed));

// Virtqueue.
struct virtio_virtq {
  struct virtq_desc descs[VIRTQ_ENTRY_NUM];
  struct virtq_avail avail;
  struct virtq_used used __attribute__((aligned(PAGE_SIZE)));
  int queue_index;
  volatile uint16_t *used_index;
  uint16_t last_used_index;
} __attribute__((packed));

// Virtio-blk request.
struct virtio_blk_req {
  uint32_t type;
  uint32_t reserved;
  uint64_t sector;
  uint8_t data[512];
  uint8_t status;
} __attribute__((packed));

struct virtio_blk_dev {
  uint32_t paddr;
  uint32_t sectors;
  struct virtio_blk_req *req;
  struct virtio_virtq *vq;
  paddr_t req_paddr;
};

static struct virtio_blk_dev devices[2];

uint32_t virtio_blk_sectors(uint32_t dev) {
  if (dev >= 2)
    return 0;
  return devices[dev].sectors;
}

uint32_t virtio_reg_read32(uint32_t dev, unsigned offset) {
  return *((volatile uint32_t *)(devices[dev].paddr + offset));
}

uint64_t virtio_reg_read64(uint32_t dev, unsigned offset) {
  return *((volatile uint64_t *)(devices[dev].paddr + offset));
}

void virtio_reg_write32(uint32_t dev, unsigned offset, uint32_t value) {
  *((volatile uint32_t *)(devices[dev].paddr + offset)) = value;
}

void virtio_reg_fetch_and_or32(uint32_t dev, unsigned offset, uint32_t value) {
  virtio_reg_write32(dev, offset, virtio_reg_read32(dev, offset) | value);
}

struct virtio_virtq *virtq_init(uint32_t dev, uint32_t index) {
  kprintf("virtq_init: sizeof(virtio_virtq) = %d\n",
          (int)sizeof(struct virtio_virtq));
  // Allocate a region for the virtqueue.
  paddr_t virtq_paddr =
      alloc_pages(align_up(sizeof(struct virtio_virtq), PAGE_SIZE) / PAGE_SIZE);
  struct virtio_virtq *vq = (struct virtio_virtq *)virtq_paddr;
  memset(vq, 0, sizeof(*vq));
  vq->queue_index = index;
  vq->used_index = (volatile uint16_t *)&vq->used.index;
  // 1. Select the queue writing its index (first queue is 0) to QueueSel.
  virtio_reg_write32(dev, VIRTIO_REG_QUEUE_SEL, index);
  // 5. Notify the device about the queue size by writing the size to QueueNum.
  virtio_reg_write32(dev, VIRTIO_REG_QUEUE_NUM, VIRTQ_ENTRY_NUM);
  // 6. Notify the device about the used alignment by writing its value in bytes
  // to QueueAlign.
  virtio_reg_write32(dev, VIRTIO_REG_QUEUE_ALIGN, 0);
  // 7. Write the physical number of the first page of the queue to the QueuePFN
  // register.
  virtio_reg_write32(dev, VIRTIO_REG_QUEUE_PFN, virtq_paddr);
  return vq;
}

// Notifies the device that there is a new request.
void virtq_kick(uint32_t dev, struct virtio_virtq *vq, int desc_index) {
  vq->avail.ring[vq->avail.index % VIRTQ_ENTRY_NUM] = desc_index;
  ++vq->avail.index;
  __sync_synchronize();
  virtio_reg_write32(dev, VIRTIO_REG_QUEUE_NOTIFY, vq->queue_index);
  ++vq->last_used_index;
}

bool virtq_is_busy(struct virtio_virtq *vq) {
  return vq->last_used_index != *vq->used_index;
}

static void read_write_device(uint32_t dev, void *buf, uint32_t sector,
                              bool is_write) {
  if (dev >= 2) {
    PANIC("virtio: invalid dev=%d\n", dev);
    return;
  }
  struct virtio_blk_dev *d = &devices[dev];
  if (sector >= d->sectors) {
    PANIC("virtio: tried to read/write sector=%d, but capacity is %d\n", sector,
          d->sectors);
    return;
  }

  // Construct the request according to the virtio-blk specification.
  d->req->sector = sector;
  d->req->type = is_write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
  if (is_write)
    memcpy(d->req->data, buf, SECTOR_SIZE);

  // Construct the virtqueue descriptors (using 3 descriptors).
  struct virtio_virtq *vq = d->vq;
  vq->descs[0].addr = d->req_paddr;
  vq->descs[0].len = sizeof(uint32_t) * 2 + sizeof(uint64_t);
  vq->descs[0].flags = VIRTQ_DESC_F_NEXT;
  vq->descs[0].next = 1;

  vq->descs[1].addr = d->req_paddr + offsetof(struct virtio_blk_req, data);
  vq->descs[1].len = SECTOR_SIZE;
  vq->descs[1].flags = VIRTQ_DESC_F_NEXT | (is_write ? 0 : VIRTQ_DESC_F_WRITE);
  vq->descs[1].next = 2;

  vq->descs[2].addr = d->req_paddr + offsetof(struct virtio_blk_req, status);
  vq->descs[2].len = sizeof(uint8_t);
  vq->descs[2].flags = VIRTQ_DESC_F_WRITE;

  // Notify the device that there is a new request.
  virtq_kick(dev, vq, 0);

  // Wait until the device finishes processing.
  while (virtq_is_busy(vq))
    ;

  // virtio-blk: If a non-zero value is returned, it's an error.
  if (d->req->status != 0) {
    PANIC("virtio: warn: failed to read/write sector=%d status=%d\n", sector,
          d->req->status);
    return;
  }

  // For read operations, copy the data into the buffer.
  if (!is_write)
    memcpy(buf, d->req->data, SECTOR_SIZE);
}

void read_device(uint32_t dev, void *buf, uint32_t sector) {
  read_write_device(dev, buf, sector, /* is_write= */ false);
}

void write_device(uint32_t dev, void *buf, uint32_t sector) {
  read_write_device(dev, buf, sector, /* is_write= */ true);
}

void virtio_blk_init_device(uint32_t dev, uint32_t paddr) {
  struct virtio_blk_dev *d = &devices[dev];
  d->paddr = paddr;

  if (virtio_reg_read32(dev, VIRTIO_REG_MAGIC) != 0x74726976)
    PANIC("virtio: invalid magic value for dev %d", dev);
  if (virtio_reg_read32(dev, VIRTIO_REG_VERSION) != 1)
    PANIC("virtio: invalid version for dev %d", dev);
  if (virtio_reg_read32(dev, VIRTIO_REG_DEVICE_ID) != VIRTIO_DEVICE_BLK)
    PANIC("virtio: invalid device id for dev %d", dev);

  // Reset the device.
  virtio_reg_write32(dev, VIRTIO_REG_DEVICE_STATUS, 0);
  // Set the ACKNOWLEDGE status bit: the guest OS has noticed the device.
  virtio_reg_fetch_and_or32(dev, VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_ACK);
  // Set the DRIVER status bit.
  virtio_reg_fetch_and_or32(dev, VIRTIO_REG_DEVICE_STATUS,
                            VIRTIO_STATUS_DRIVER);
  // Set the FEATURES_OK status bit.
  virtio_reg_fetch_and_or32(dev, VIRTIO_REG_DEVICE_STATUS,
                            VIRTIO_STATUS_FEAT_OK);
  // Perform device-specific setup, including discovery of virtqueues for the
  // device
  d->vq = virtq_init(dev, 0);
  // Set the DRIVER_OK status bit.
  virtio_reg_write32(dev, VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_DRIVER_OK);

  // Get the disk capacity.
  d->sectors = virtio_reg_read64(dev, VIRTIO_REG_DEVICE_CONFIG + 0);

  // Allocate a region to store requests to the device.
  d->req_paddr =
      alloc_pages(align_up(sizeof(*(d->req)), PAGE_SIZE) / PAGE_SIZE);
  d->req = (struct virtio_blk_req *)d->req_paddr;
}

void virtio_blk_init() {
  virtio_blk_init_device(0, VIRTIO_BLK0_PADDR);
  virtio_blk_init_device(1, VIRTIO_BLK1_PADDR);
}