# U-Boot boot script for dmaOS on StarFive VisionFive 2 (JH7110)
# Compile to boot.scr using: mkimage -C none -A riscv -T script -d boot.cmd boot.scr

echo "=================================================="
echo "    Booting dmaOS on StarFive VisionFive 2        "
echo "=================================================="

setenv kernel_addr 0x40200000
setenv initrd_addr 0x46000000

echo "Loading dmaOS kernel.bin to ${kernel_addr}..."
fatload mmc 1:3 ${kernel_addr} kernel.bin

echo "Loading dmaOS disk.img (Initrd) to ${initrd_addr}..."
fatload mmc 1:3 ${initrd_addr} disk.img

echo "Starting dmaOS..."
go ${kernel_addr}
