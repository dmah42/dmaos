# VisionFive 2 Setup

How to put dmaOS on an SD card and boot it on a StarFive VisionFive 2.

> **Status:** untested on hardware. dmaOS doesn't use the display or keyboard
> on the VisionFive 2 yet (see [Limitations](#limitations)).

## What you need

- VisionFive 2 board and a USB-C power supply.
- microSD card (it gets erased).
- HDMI display and USB keyboard.

## 1. Set the boot switches

Set the boot-mode switches to QSPI flash (both `L`), the factory setting.

## 2. Build

```bash
make ARCH=riscv64 BOARD=visionfive2 all
```

## 3. Write the SD card

Find the card with `diskutil list` (e.g. `/dev/disk4`), then:

```bash
sudo tools/create_sdcard.sh /dev/disk4
```

## 4. Boot

Insert the card, connect the display and keyboard, and power on.

## Limitations

- **Display:** dmaOS doesn't take over U-Boot's framebuffer yet.
- **Keyboard:** no USB keyboard driver yet.
- **Network:** no Ethernet driver yet.
