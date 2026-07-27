#include "virtio.h"

#include "kernel.h"
#include "page.h"
#include "process.h"
#include "stdlib.h"

#define VIRTIO_MAGIC_NUMBER 0x74726976

// VirtIO MMIO Register Offsets (Byte offsets from device base address)
#define VIRTIO_REG_MAGIC 0x000     // Magic value (0x74726976)
#define VIRTIO_REG_VERSION 0x004   // Device version (1 for Legacy)
#define VIRTIO_REG_DEVICE_ID 0x008 // Subsystem Device ID (1 for Network)
#define VIRTIO_REG_VENDOR_ID 0x00c // Subsystem Vendor ID

#define VIRTIO_REG_DEVICE_FEATURES 0x010 // Device capabilities
#define VIRTIO_REG_DEVICE_FEAT_SEL 0x014 // Which 32-bit feature word to read
#define VIRTIO_REG_DRIVER_FEATURES 0x020 // Flags activated by the driver
#define VIRTIO_REG_DRIVER_FEAT_SEL 0x024 // Which 32-bit feature word to write

#define VIRTIO_REG_QUEUE_SEL 0x030     // Selects a specific virtqueue index
#define VIRTIO_REG_QUEUE_NUM_MAX 0x034 // Max queue size supported by device
#define VIRTIO_REG_QUEUE_NUM 0x038     // Queue size configuration
#define VIRTIO_REG_QUEUE_ALIGN 0x03c   // Memory alignment for the used ring
#define VIRTIO_REG_QUEUE_PFN 0x040     // Page Frame Number of queue
#define VIRTIO_REG_QUEUE_READY 0x44

#define VIRTIO_REG_QUEUE_NOTIFY 0x050     // Notify device of new descriptors
#define VIRTIO_REG_INTERRUPT_STATUS 0x060 // Why an interrupt was triggered
#define VIRTIO_REG_INTERRUPT_ACK 0x064    // Clears / Acks the handled interrupt
#define VIRTIO_REG_DEVICE_STATUS 0x070    // Initialization state of the device
#define VIRTIO_REG_DEVICE_CONFIG 0x100

// Standard VirtIO Device Status Bits (Written to VIRTIO_REG_DEVICE_STATUS)
#define VIRTIO_STATUS_ACK 1         // OS has recognized the device
#define VIRTIO_STATUS_DRIVER 2      // OS knows how to drive the device
#define VIRTIO_STATUS_DRIVER_OK 128 // Driver is initialized and running
#define VIRTIO_STATUS_FEATURES_OK 8 // Driver features are acked by the device
#define VIRTIO_STATUS_FAILED 128    // Something broke; tell host to abort

#define VIRTQ_DESC_F_NEXT 1
#define VIRTQ_DESC_F_WRITE 2

#define VIRTQ_AVAIL_F_NO_INTERRUPT 1

#define VIRTIO_BLK_T_IN 0
#define VIRTIO_BLK_T_OUT 1

#define VIRTIO_DEVICE_BLK 2

#define VIRTQ_BLK_ENTRY_NUM 16

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
  uint16_t ring[VIRTQ_BLK_ENTRY_NUM];
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
  struct virtq_used_elem ring[VIRTQ_BLK_ENTRY_NUM];
} __attribute__((packed));

// Virtqueue.
struct virtio_virtq {
  struct virtq_desc descs[VIRTQ_BLK_ENTRY_NUM];
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
  virtio_reg_write32(dev, VIRTIO_REG_QUEUE_NUM, VIRTQ_BLK_ENTRY_NUM);
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
  vq->avail.ring[vq->avail.index % VIRTQ_BLK_ENTRY_NUM] = desc_index;
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

  if (virtio_reg_read32(dev, VIRTIO_REG_MAGIC) != VIRTIO_MAGIC_NUMBER)
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
                            VIRTIO_STATUS_FEATURES_OK);
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

// Input driver (keyboard)
#define VIRTIO_DEVICE_INPUT 18
#define EV_KEY 0x01

#define VIRTQ_INPUT_ENTRY_NUM 16

struct virtio_input_event {
  uint16_t type;
  uint16_t code;
  uint32_t value;
} __attribute__((packed));

struct virtio_input_dev {
  uint32_t paddr;
  struct virtio_virtq *vq;
  struct virtio_input_event *events;
  paddr_t events_paddr;
};

static struct virtio_input_dev input_dev;

static uint32_t input_reg_read32(unsigned offset) {
  return *((volatile uint32_t *)(input_dev.paddr + offset));
}

static void input_reg_write32(unsigned offset, uint32_t value) {
  *((volatile uint32_t *)(input_dev.paddr + offset)) = value;
}

static void input_reg_fetch_and_or32(unsigned offset, uint32_t value) {
  input_reg_write32(offset, input_reg_read32(offset) | value);
}

void virtio_input_init(void) {
  input_dev.paddr = VIRTIO_INPUT_PADDR;

  if (input_reg_read32(VIRTIO_REG_MAGIC) != VIRTIO_MAGIC_NUMBER)
    PANIC("virtio-input: invalid magic");
  if (input_reg_read32(VIRTIO_REG_VERSION) != 1)
    PANIC("virtio-input: invalid version");
  if (input_reg_read32(VIRTIO_REG_DEVICE_ID) != VIRTIO_DEVICE_INPUT)
    PANIC("virtio-input: invalid device id");

  input_reg_write32(VIRTIO_REG_DEVICE_STATUS, 0);
  input_reg_fetch_and_or32(VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_ACK);
  input_reg_fetch_and_or32(VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_DRIVER);
  input_reg_fetch_and_or32(VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_FEATURES_OK);

  // Allocate the virtqueue using your existing queue initialization logic
  paddr_t virtq_paddr =
      alloc_pages(align_up(sizeof(struct virtio_virtq), PAGE_SIZE) / PAGE_SIZE);
  struct virtio_virtq *vq = (struct virtio_virtq *)virtq_paddr;
  memset(vq, 0, sizeof(*vq));
  vq->queue_index = 0;
  vq->used_index = (volatile uint16_t *)&vq->used.index;

  input_reg_write32(VIRTIO_REG_QUEUE_SEL, 0);
  input_reg_write32(VIRTIO_REG_QUEUE_NUM, VIRTQ_INPUT_ENTRY_NUM);
  input_reg_write32(VIRTIO_REG_QUEUE_ALIGN, 0);
  input_reg_write32(VIRTIO_REG_QUEUE_PFN, virtq_paddr);

  input_dev.vq = vq;

  input_reg_write32(VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_DRIVER_OK);

  input_dev.events_paddr = alloc_pages(1);
  input_dev.events = (struct virtio_input_event *)input_dev.events_paddr;
  memset(input_dev.events, 0, PAGE_SIZE);

  for (int i = 0; i < VIRTQ_INPUT_ENTRY_NUM; i++) {
    vq->descs[i].addr =
        input_dev.events_paddr + (i * sizeof(struct virtio_input_event));
    vq->descs[i].len = sizeof(struct virtio_input_event);
    vq->descs[i].flags = VIRTQ_DESC_F_WRITE;
    vq->avail.ring[i] = i;
  }
  vq->avail.index = VIRTQ_INPUT_ENTRY_NUM;
  __sync_synchronize();
  input_reg_write32(VIRTIO_REG_QUEUE_NOTIFY, vq->queue_index);
}

bool virtio_input_poll_event(uint16_t *code, int *pressed) {
  struct virtio_virtq *vq = input_dev.vq;

  if (vq->last_used_index == *vq->used_index)
    return false;

  uint32_t desc_idx =
      vq->used.ring[vq->last_used_index % VIRTQ_INPUT_ENTRY_NUM].id;
  ++vq->last_used_index;

  struct virtio_input_event *evt = &input_dev.events[desc_idx];

  vq->avail.ring[vq->avail.index % VIRTQ_INPUT_ENTRY_NUM] = desc_idx;
  ++vq->avail.index;
  __sync_synchronize();
  input_reg_write32(VIRTIO_REG_QUEUE_NOTIFY, vq->queue_index);

  if (evt->type == EV_KEY) {
    *code = evt->code;
    *pressed = (evt->value > 0) ? 1 : 0;
    return true;
  }

  return false;
}

// Network
#define VIRTIO_DEVICE_NET 1

#define VIRTQ_NET_ENTRY_NUM 128

struct virtq_net_avail {
  uint16_t flags;
  uint16_t idx;
  uint16_t ring[VIRTQ_NET_ENTRY_NUM];
} __attribute__((packed));

struct virtq_used_item {
  uint32_t id;
  uint32_t len;
} __attribute__((packed));

struct virtq_net_used {
  uint16_t flags;
  uint16_t idx;
  struct virtq_used_item ring[VIRTQ_NET_ENTRY_NUM];
} __attribute__((packed));

// The overall runtime wrapper for your driver state
struct virtio_net_queue {
  uint32_t queue_index;
  uint16_t last_used_idx;

  struct virtq_desc *desc;
  struct virtq_net_avail *avail;
  struct virtq_net_used *used;

  void *buffers[VIRTQ_NET_ENTRY_NUM];
};

struct virtio_net_header {
  uint8_t flags;
  uint8_t gso_type;
  uint16_t len;
  uint16_t gso_size;
  uint16_t csum_start;
  uint16_t csum_offset;
} __attribute__((packed));

struct virtio_net_dev {
  uint32_t paddr;
  struct virtio_net_queue rx_vq;
  struct virtio_net_queue tx_vq;
};

static struct virtio_net_dev net_dev;

static uint32_t net_reg_read32(unsigned offset) {
  return *((volatile uint32_t *)(net_dev.paddr + offset));
}

static void net_reg_write32(unsigned offset, uint32_t value) {
  *((volatile uint32_t *)(net_dev.paddr + offset)) = value;
}

static void net_reg_fetch_and_or32(unsigned offset, uint32_t value) {
  net_reg_write32(offset, net_reg_read32(offset) | value);
}

void net_setup_queue_memory(struct virtio_net_queue *vq, uint32_t q_index) {
  vq->queue_index = q_index;
  vq->last_used_idx = 0;

  // Calculate memory needed for one legacy queue
  size_t desc_sz = sizeof(struct virtq_desc) * VIRTQ_NET_ENTRY_NUM;
  size_t avail_sz = 6 + (2 * VIRTQ_NET_ENTRY_NUM);
  size_t part1_sz = align_up(desc_sz + avail_sz, PAGE_SIZE);
  size_t used_sz = align_up(6 + (8 * VIRTQ_NET_ENTRY_NUM), PAGE_SIZE);
  size_t total_sz = part1_sz + used_sz;

  paddr_t queue_paddr = alloc_pages(total_sz / PAGE_SIZE);
  memset((void *)queue_paddr, 0, total_sz);

  // Bind layout pointers to the allocated flat memory block
  vq->desc = (struct virtq_desc *)queue_paddr;
  vq->avail = (struct virtq_net_avail *)(queue_paddr + desc_sz);
  vq->used = (struct virtq_net_used *)(queue_paddr + part1_sz);

  // Register with hardware
  net_reg_write32(VIRTIO_REG_QUEUE_SEL, q_index);
  net_reg_write32(VIRTIO_REG_QUEUE_NUM, VIRTQ_NET_ENTRY_NUM);
  net_reg_write32(VIRTIO_REG_QUEUE_ALIGN, PAGE_SIZE);

  // Crucial: Pass Page Frame Number (paddr / PAGE_SIZE) for Legacy!
  net_reg_write32(VIRTIO_REG_QUEUE_PFN, queue_paddr / PAGE_SIZE);
}

void virtio_net_init() {
  net_dev.paddr = VIRTIO_NET_PADDR;

  // 1. Basic Validations
  if (net_reg_read32(VIRTIO_REG_MAGIC) != VIRTIO_MAGIC_NUMBER)
    PANIC("Invalid magic");
  if (net_reg_read32(VIRTIO_REG_VERSION) != 1)
    PANIC("Only Legacy supported");
  if (net_reg_read32(VIRTIO_REG_DEVICE_ID) != VIRTIO_DEVICE_NET)
    PANIC("Not a net device");

  net_reg_write32(VIRTIO_REG_DEVICE_STATUS, 0);
  net_reg_fetch_and_or32(VIRTIO_REG_DEVICE_STATUS, 1);
  net_reg_fetch_and_or32(VIRTIO_REG_DEVICE_STATUS, 2);

  uint32_t features = net_reg_read32(VIRTIO_REG_DEVICE_FEATURES);
  features &= ~(1 << 5);
  net_reg_write32(VIRTIO_REG_DRIVER_FEATURES, features);

  net_reg_fetch_and_or32(VIRTIO_REG_DEVICE_STATUS, 8); // FEATURES_OK

  net_setup_queue_memory(&net_dev.rx_vq, 0); // RX Queue
  net_setup_queue_memory(&net_dev.tx_vq, 1); // TX Queue

  // Fill RX Descriptors
  for (int i = 0; i < VIRTQ_NET_ENTRY_NUM; ++i) {
    paddr_t buf = alloc_pages(1); // 4096 bytes is plenty
    net_dev.rx_vq.buffers[i] = (void *)buf;

    net_dev.rx_vq.desc[i].addr = buf;
    net_dev.rx_vq.desc[i].len = PAGE_SIZE;
    net_dev.rx_vq.desc[i].flags = VIRTQ_DESC_F_WRITE;
    net_dev.rx_vq.desc[i].next = 0;

    net_dev.rx_vq.avail->ring[i] = i;
  }
  net_dev.rx_vq.avail->idx = VIRTQ_NET_ENTRY_NUM;

  net_reg_write32(VIRTIO_REG_DEVICE_STATUS,
                  VIRTIO_STATUS_DRIVER_OK); // DRIVER_OK

  net_reg_write32(VIRTIO_REG_QUEUE_NOTIFY, 0);
}

static uint16_t get_free_tx_descriptor(struct virtio_net_queue *tx_q) {
  static uint16_t next_free_idx = 0;

  // Choose the slot we want to use next
  uint16_t desc_idx = next_free_idx;

  // Safety Check: Look at the device's "used" ring.
  // If the device hasn't processed this descriptor yet, the queue is completely
  // full.
  while (tx_q->last_used_idx == tx_q->used->idx) {
    yield();
  }

  // Advance our allocator index for the next syscall invocation
  next_free_idx = (next_free_idx + 1) % VIRTQ_NET_ENTRY_NUM;

  return desc_idx;
}

int virtio_net_send(const uint8_t *buf, uint32_t len) {
  struct virtio_net_queue *tx_q = &net_dev.tx_vq;
  uint16_t desc_idx = get_free_tx_descriptor(tx_q);

  // Clear the mandatory VirtIO network header inside the target buffer
  struct virtio_net_header *hdr =
      (struct virtio_net_header *)tx_q->buffers[desc_idx];
  memset(hdr, 0, sizeof(struct virtio_net_header));

  // 3. Safely copy the user space packet payload right after the header
  void *packet_payload_dest =
      (void *)((uintptr_t)hdr + sizeof(struct virtio_net_header));
  memcpy(packet_payload_dest, buf, len);

  // 4. Update the descriptor table entries so QEMU knows what to read
  tx_q->desc[desc_idx].addr = (uintptr_t)hdr;
  tx_q->desc[desc_idx].len = sizeof(struct virtio_net_header) + len;
  tx_q->desc[desc_idx].flags = 0;
  tx_q->desc[desc_idx].next = 0;

  // Update available ring buffer
  uint16_t avail_idx = tx_q->avail->idx % VIRTQ_NET_ENTRY_NUM;
  tx_q->avail->ring[avail_idx] = desc_idx;

  __sync_synchronize();
  ++tx_q->avail->idx;

  net_reg_write32(VIRTIO_REG_QUEUE_NOTIFY, tx_q->queue_index);

  return len;
}

static bool is_packet_available(struct virtio_net_queue *rx_q) {
  // If our local tracker matches the hardware index, no new packets have
  // arrived
  return rx_q->last_used_idx != rx_q->used->idx;
}

int virtio_net_recv(uint8_t *buf, uint32_t len) {
  struct virtio_net_queue *rx_q = &net_dev.rx_vq;

  while (!is_packet_available(rx_q)) {
    yield();
  }

  uint16_t used_slot = rx_q->last_used_idx % VIRTQ_NET_ENTRY_NUM;
  uint32_t desc_idx = rx_q->used->ring[used_slot].id;
  uint32_t total_len = rx_q->used->ring[used_slot].len;

  struct virtio_net_header *hdr =
      (struct virtio_net_header *)rx_q->buffers[desc_idx];
  void *packet_payload_src =
      (void *)((uintptr_t)hdr + sizeof(struct virtio_net_header));

  // Calculate actual ethernet frame length (subtracting the VirtIO header size)
  if (total_len <= sizeof(struct virtio_net_header)) {
    // Length is too short to be a real packet.
    return -1;
  }
  size_t packet_len = total_len - sizeof(struct virtio_net_header);

  // Enforce user buffer boundaries to prevent memory overflow
  size_t copy_size = (packet_len > len) ? len : packet_len;

  memcpy(buf, packet_payload_src, copy_size);

  rx_q->desc[desc_idx].len = PAGE_SIZE;
  rx_q->desc[desc_idx].flags = 2;

  uint16_t avail_slot = rx_q->avail->idx % VIRTQ_NET_ENTRY_NUM;
  rx_q->avail->ring[avail_slot] = desc_idx;

  __sync_synchronize();
  ++rx_q->avail->idx;
  ++rx_q->last_used_idx;

  net_reg_write32(VIRTIO_REG_QUEUE_NOTIFY, rx_q->queue_index);

  return (int)copy_size; // Return the exact number of bytes delivered
}