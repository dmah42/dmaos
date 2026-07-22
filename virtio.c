#include "virtio.h"

#include "kernel.h"
#include "memory.h"
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

static uint32_t blk_sectors;
struct virtio_blk_req *blk_req;
struct virtio_virtq *blk_request_vq;
paddr_t blk_req_paddr;

uint32_t virtio_blk_sectors(void) { return blk_sectors; }

uint32_t virtio_reg_read32(unsigned offset) {
  return *((volatile uint32_t *)(VIRTIO_BLK_PADDR + offset));
}

uint64_t virtio_reg_read64(unsigned offset) {
  return *((volatile uint64_t *)(VIRTIO_BLK_PADDR + offset));
}

void virtio_reg_write32(unsigned offset, uint32_t value) {
  *((volatile uint32_t *)(VIRTIO_BLK_PADDR + offset)) = value;
}

void virtio_reg_fetch_and_or32(unsigned offset, uint32_t value) {
  virtio_reg_write32(offset, virtio_reg_read32(offset) | value);
}

struct virtio_virtq *virtq_init(uint32_t index) {
  // Allocate a region for the virtqueue.
  paddr_t virtq_paddr =
      alloc_pages(align_up(sizeof(struct virtio_virtq), PAGE_SIZE) / PAGE_SIZE);
  struct virtio_virtq *vq = (struct virtio_virtq *)virtq_paddr;
  memset(vq, 0, sizeof(*vq));
  vq->queue_index = index;
  vq->used_index = (volatile uint16_t *)&vq->used.index;
  // 1. Select the queue writing its index (first queue is 0) to QueueSel.
  virtio_reg_write32(VIRTIO_REG_QUEUE_SEL, index);
  // 5. Notify the device about the queue size by writing the size to QueueNum.
  virtio_reg_write32(VIRTIO_REG_QUEUE_NUM, VIRTQ_ENTRY_NUM);
  // 6. Notify the device about the used alignment by writing its value in bytes
  // to QueueAlign.
  virtio_reg_write32(VIRTIO_REG_QUEUE_ALIGN, 0);
  // 7. Write the physical number of the first page of the queue to the QueuePFN
  // register.
  virtio_reg_write32(VIRTIO_REG_QUEUE_PFN, virtq_paddr);
  return vq;
}

// Notifies the device that there is a new request.
void virtq_kick(struct virtio_virtq *vq, int desc_index) {
  vq->avail.ring[vq->avail.index % VIRTQ_ENTRY_NUM] = desc_index;
  vq->avail.index++;
  __sync_synchronize();
  virtio_reg_write32(VIRTIO_REG_QUEUE_NOTIFY, vq->queue_index);
  vq->last_used_index++;
}

bool virtq_is_busy(struct virtio_virtq *vq) {
  return vq->last_used_index != *vq->used_index;
}

void read_write_device(void *buf, uint32_t sector, bool is_write) {
  if (sector >= blk_sectors) {
    printf("virtio: tried to read/write sector=%d, but capacity is %d\n",
           sector, blk_sectors);
    return;
  }

  // Construct the request according to the virtio-blk specification.
  blk_req->sector = sector;
  blk_req->type = is_write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
  if (is_write)
    memcpy(blk_req->data, buf, SECTOR_SIZE);

  // Construct the virtqueue descriptors (using 3 descriptors).
  struct virtio_virtq *vq = blk_request_vq;
  vq->descs[0].addr = blk_req_paddr;
  vq->descs[0].len = sizeof(uint32_t) * 2 + sizeof(uint64_t);
  vq->descs[0].flags = VIRTQ_DESC_F_NEXT;
  vq->descs[0].next = 1;

  vq->descs[1].addr = blk_req_paddr + offsetof(struct virtio_blk_req, data);
  vq->descs[1].len = SECTOR_SIZE;
  vq->descs[1].flags = VIRTQ_DESC_F_NEXT | (is_write ? 0 : VIRTQ_DESC_F_WRITE);
  vq->descs[1].next = 2;

  vq->descs[2].addr = blk_req_paddr + offsetof(struct virtio_blk_req, status);
  vq->descs[2].len = sizeof(uint8_t);
  vq->descs[2].flags = VIRTQ_DESC_F_WRITE;

  // Notify the device that there is a new request.
  virtq_kick(vq, 0);

  // Wait until the device finishes processing.
  while (virtq_is_busy(vq))
    ;

  // virtio-blk: If a non-zero value is returned, it's an error.
  if (blk_req->status != 0) {
    printf("virtio: warn: failed to read/write sector=%d status=%d\n", sector,
           blk_req->status);
    return;
  }

  // For read operations, copy the data into the buffer.
  if (!is_write)
    memcpy(buf, blk_req->data, SECTOR_SIZE);
}

void virtio_blk_init() {
  if (virtio_reg_read32(VIRTIO_REG_MAGIC) != 0x74726976)
    PANIC("virtio: invalid magic value");
  if (virtio_reg_read32(VIRTIO_REG_VERSION) != 1)
    PANIC("virtio: invalid version");
  if (virtio_reg_read32(VIRTIO_REG_DEVICE_ID) != VIRTIO_DEVICE_BLK)
    PANIC("virtio: invalid device id");

  // 1. Reset the device.
  virtio_reg_write32(VIRTIO_REG_DEVICE_STATUS, 0);
  // 2. Set the ACKNOWLEDGE status bit: the guest OS has noticed the device.
  virtio_reg_fetch_and_or32(VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_ACK);
  // 3. Set the DRIVER status bit.
  virtio_reg_fetch_and_or32(VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_DRIVER);
  // 5. Set the FEATURES_OK status bit.
  virtio_reg_fetch_and_or32(VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_FEAT_OK);
  // 7. Perform device-specific setup, including discovery of virtqueues for the
  // device
  blk_request_vq = virtq_init(0);
  // 8. Set the DRIVER_OK status bit.
  virtio_reg_write32(VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_DRIVER_OK);

  // Get the disk capacity.
  blk_sectors = virtio_reg_read64(VIRTIO_REG_DEVICE_CONFIG + 0);

  // Allocate a region to store requests to the device.
  blk_req_paddr =
      alloc_pages(align_up(sizeof(*blk_req), PAGE_SIZE) / PAGE_SIZE);
  blk_req = (struct virtio_blk_req *)blk_req_paddr;
}