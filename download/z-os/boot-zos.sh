#!/bin/sh
# Z/OS - QEMU Launch Script
# This script boots Z/OS using QEMU's direct kernel boot

KERNEL="bzImage"
INITRD="initramfs.cpio.gz"

echo "  ╔══════════════════════════════════════════╗"
echo "  ║          Booting Z/OS with QEMU          ║"
echo "  ╚══════════════════════════════════════════╝"
echo ""
echo "  Kernel:  $KERNEL"
echo "  Initrd:  $INITRD"
echo "  Memory:  128MB"
echo ""

# Try qemu-system-x86_64 first, then fall back
QEMU=""
for cmd in qemu-system-x86_64 qemu-system-x86; do
    if command -v $cmd >/dev/null 2>&1; then
        QEMU=$cmd
        break
    fi
done

if [ -z "$QEMU" ]; then
    echo "ERROR: QEMU not found. Install with:"
    echo "  Ubuntu/Debian: sudo apt install qemu-system-x86"
    echo "  Fedora:        sudo dnf install qemu-system-x86"
    echo "  macOS:         brew install qemu"
    exit 1
fi

exec $QEMU \
    -kernel "$KERNEL" \
    -initrd "$INITRD" \
    -append "console=ttyS0 console=tty0 quiet" \
    -m 128M \
    -nographic \
    -no-reboot
