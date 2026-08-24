#!/usr/bin/env bash
set -euo pipefail

# ==============================================================================
# dmaOS Generic SD Card Creation & Flashing Utility
# ==============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

usage() {
    echo "Usage: $0 [options] <target_disk_device>"
    echo ""
    echo "Options:"
    echo "  --platform <name>   Target platform (default: visionfive2)"
    echo "  --spl <path>        Path to SPL binary (e.g. u-boot-spl.bin.normal.out)"
    echo "  --uboot <path>      Path to U-Boot payload (e.g. visionfive2_fw_payload.img)"
    echo "  --help              Display this help message"
    echo ""
    echo "Examples:"
    echo "  Linux:  sudo $0 --platform visionfive2 /dev/sdb"
    echo "  macOS:  sudo $0 --platform visionfive2 /dev/disk2"
    exit 1
}

PLATFORM="visionfive2"
SPL_BIN=""
UBOOT_IMG=""
TARGET_DEV=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --platform)
            PLATFORM="$2"
            shift 2
            ;;
        --spl)
            SPL_BIN="$2"
            shift 2
            ;;
        --uboot)
            UBOOT_IMG="$2"
            shift 2
            ;;
        --help|-h)
            usage
            ;;
        -*)
            echo "Unknown option: $1"
            usage
            ;;
        *)
            TARGET_DEV="$1"
            shift
            ;;
    esac
done

if [[ -z "${TARGET_DEV}" ]]; then
    echo "ERROR: Target disk device is required."
    usage
fi

PLATFORM_DIR="${REPO_ROOT}/platform/${PLATFORM}"
if [[ ! -d "${PLATFORM_DIR}" ]]; then
    echo "ERROR: Platform directory not found: ${PLATFORM_DIR}"
    exit 1
fi

if [[ -f "${PLATFORM_DIR}/platform.env" ]]; then
    # shellcheck source=/dev/null
    source "${PLATFORM_DIR}/platform.env"
fi

BUILD_DIR="${REPO_ROOT}/build/${ARCH:-rv64}-vf2"

echo "=================================================="
echo "    dmaOS SD Card Flashing Utility               "
echo "=================================================="
echo "Platform     : ${PLATFORM}"
echo "Target Device: ${TARGET_DEV}"
echo "Build Dir    : ${BUILD_DIR}"
echo ""

# Safety Check: Prevent formatting internal OS drive
if [[ "${TARGET_DEV}" == "/dev/sda" || "${TARGET_DEV}" == "/dev/nvme0n1" || "${TARGET_DEV}" == "/dev/disk0" || "${TARGET_DEV}" == "/dev/disk1" ]]; then
    echo "ERROR: Refusing to write to potentially internal system drive: ${TARGET_DEV}"
    exit 1
fi

if [[ ! -f "${BUILD_DIR}/kernel.bin" ]]; then
    echo "Building dmaOS for platform '${PLATFORM}'..."
    make -C "${REPO_ROOT}" ARCH="${ARCH:-riscv64}" BOARD="${PLATFORM}" all
fi

read -p "WARNING: ALL DATA ON ${TARGET_DEV} WILL BE ERASED! Proceed? (y/N): " -r CONFIRM
if [[ ! "${CONFIRM}" =~ ^[Yy]$ ]]; then
    echo "Aborted."
    exit 0
fi

OS_TYPE="$(uname -s)"

echo "==> Partitioning ${TARGET_DEV}..."
if [[ "${OS_TYPE}" == "Linux" ]]; then
    # Partition layout via parted on Linux
    parted -s "${TARGET_DEV}" mklabel gpt
    parted -s "${TARGET_DEV}" mkpart spl 0% 2MB
    parted -s "${TARGET_DEV}" mkpart uboot 2MB 6MB
    parted -s "${TARGET_DEV}" mkpart boot fat32 6MB 70MB
    parted -s "${TARGET_DEV}" mkpart home 70MB 100%
    
    sync
    sleep 1

    BOOT_PART="${TARGET_DEV}3"
    HOME_PART="${TARGET_DEV}4"
    if [[ "${TARGET_DEV}" =~ [0-9]$ ]]; then
        BOOT_PART="${TARGET_DEV}p3"
        HOME_PART="${TARGET_DEV}p4"
    fi

    # Flash SPL & U-Boot if provided
    if [[ -n "${SPL_BIN}" && -f "${SPL_BIN}" ]]; then
        echo "==> Flashing SPL to partition 1..."
        dd if="${SPL_BIN}" of="${TARGET_DEV}1" bs=1M status=progress
    fi
    if [[ -n "${UBOOT_IMG}" && -f "${UBOOT_IMG}" ]]; then
        echo "==> Flashing U-Boot payload to partition 2..."
        dd if="${UBOOT_IMG}" of="${TARGET_DEV}2" bs=1M status=progress
    fi

    echo "==> Formatting boot partition (${BOOT_PART})..."
    mkfs.vfat -F 32 -n "DMAOS_BOOT" "${BOOT_PART}"

    echo "==> Mounting boot partition..."
    MOUNT_POINT="$(mktemp -d)"
    mount "${BOOT_PART}" "${MOUNT_POINT}"

    echo "==> Copying dmaOS binaries and configuration..."
    cp "${BUILD_DIR}/kernel.bin" "${MOUNT_POINT}/kernel.bin"
    cp "${BUILD_DIR}/disk.img" "${MOUNT_POINT}/disk.img"
    if [[ -f "${PLATFORM_DIR}/uEnv.txt" ]]; then
        cp "${PLATFORM_DIR}/uEnv.txt" "${MOUNT_POINT}/uEnv.txt"
    fi

    if which mkimage >/dev/null 2>&1 && [[ -f "${PLATFORM_DIR}/boot.cmd" ]]; then
        mkimage -C none -A riscv -T script -d "${PLATFORM_DIR}/boot.cmd" "${MOUNT_POINT}/boot.scr"
    fi

    sync
    umount "${MOUNT_POINT}"
    rmdir "${MOUNT_POINT}"

    if [[ -f "${BUILD_DIR}/data.img" ]]; then
        echo "==> Flashing persistent /home filesystem template to ${HOME_PART}..."
        dd if="${BUILD_DIR}/data.img" of="${HOME_PART}" bs=1M status=progress
    fi

elif [[ "${OS_TYPE}" == "Darwin" ]]; then
    # macOS partitioning
    diskutil unmountDisk "${TARGET_DEV}" || true
    gpt destroy "${TARGET_DEV}" || true
    gpt create "${TARGET_DEV}"
    
    # SPL & U-Boot GUIDs
    SPL_GUID="${GPT_SPL_GUID:-2E54B353-1271-4842-806F-E436D6AF6985}"
    UBOOT_GUID="${GPT_UBOOT_GUID:-5B193300-FC78-40CD-8002-E86C45580B47}"

    gpt add -b 4096 -s 4096 -t "${SPL_GUID}" "${TARGET_DEV}" || true
    gpt add -b 8192 -s 8192 -t "${UBOOT_GUID}" "${TARGET_DEV}" || true
    gpt add -b 16384 -s 131072 -t EBD0A0A2-B9E5-4433-87C0-68B6B72699C7 "${TARGET_DEV}"
    gpt add -b 147456 -s 131072 -t 0FC63DAF-8483-4772-8E79-3D69D8477DE4 "${TARGET_DEV}" || true

    BOOT_PART="${TARGET_DEV}s3"
    HOME_PART="${TARGET_DEV}s4"

    newfs_msdos -F 32 -v "DMAOS_BOOT" "${BOOT_PART}"
    
    MOUNT_POINT="$(mktemp -d)"
    mount -t msdos "${BOOT_PART}" "${MOUNT_POINT}"
    
    cp "${BUILD_DIR}/kernel.bin" "${MOUNT_POINT}/kernel.bin"
    cp "${BUILD_DIR}/disk.img" "${MOUNT_POINT}/disk.img"
    if [[ -f "${PLATFORM_DIR}/uEnv.txt" ]]; then
        cp "${PLATFORM_DIR}/uEnv.txt" "${MOUNT_POINT}/uEnv.txt"
    fi
    
    if which mkimage >/dev/null 2>&1 && [[ -f "${PLATFORM_DIR}/boot.cmd" ]]; then
        mkimage -C none -A riscv -T script -d "${PLATFORM_DIR}/boot.cmd" "${MOUNT_POINT}/boot.scr"
    fi

    sync
    umount "${MOUNT_POINT}"
    rmdir "${MOUNT_POINT}"

    if [[ -f "${BUILD_DIR}/data.img" ]]; then
        echo "==> Flashing persistent /home filesystem template to ${HOME_PART}..."
        dd if="${BUILD_DIR}/data.img" of="${HOME_PART}" bs=1m
    fi
fi

echo "=================================================="
echo "  dmaOS SD Card Flashed Successfully for ${PLATFORM}! "
echo "=================================================="
