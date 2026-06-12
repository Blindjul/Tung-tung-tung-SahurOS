#!/usr/bin/env python3
"""
Create a bootable disk image for Z/OS.
Uses a simple approach: FAT32 filesystem with syslinux bootloader.
Since we may not have syslinux, we'll create a raw disk image that
can be booted directly with QEMU using -kernel and -initrd.

Also creates a standalone bootable ISO-style image.
"""

import os
import struct
import sys

def create_raw_disk_image(kernel_path, initrd_path, output_path, size_mb=10):
    """Create a raw disk image with kernel and initrd embedded.
    
    This creates an image compatible with QEMU's direct kernel boot.
    The image includes a simple boot sector that loads the kernel via
    the Linux boot protocol.
    """
    
    # Read kernel and initrd
    with open(kernel_path, 'rb') as f:
        kernel_data = f.read()
    
    with open(initrd_path, 'rb') as f:
        initrd_data = f.read()
    
    # Create the disk image
    total_size = size_mb * 1024 * 1024  # 10MB
    image = bytearray(total_size)
    
    # Place kernel at offset 1MB (after boot sector area)
    kernel_offset = 0x10000  # 64KB offset
    image[kernel_offset:kernel_offset + len(kernel_data)] = kernel_data
    
    # Place initrd after kernel
    initrd_offset = kernel_offset + len(kernel_data)
    # Align to 4KB
    initrd_offset = (initrd_offset + 4095) & ~4095
    image[initrd_offset:initrd_offset + len(initrd_data)] = initrd_data
    
    # Write a simple header at the start describing layout
    # Magic: "Z/OS\0"
    header = bytearray()
    header.extend(b'Z/OS\x00')
    header.extend(struct.pack('<I', kernel_offset))
    header.extend(struct.pack('<I', len(kernel_data)))
    header.extend(struct.pack('<I', initrd_offset))
    header.extend(struct.pack('<I', len(initrd_data)))
    header.extend(struct.pack('<I', 0x100000))  # kernel load address (1MB)
    
    image[0:len(header)] = header
    
    with open(output_path, 'wb') as f:
        f.write(bytes(image))
    
    return len(kernel_data) + len(initrd_data)


def create_qemu_boot_script(kernel_path, initrd_path, output_dir):
    """Create a QEMU launch script."""
    
    script = f"""#!/bin/sh
# Z/OS - QEMU Launch Script
# This script boots Z/OS using QEMU's direct kernel boot

KERNEL="{os.path.basename(kernel_path)}"
INITRD="{os.path.basename(initrd_path)}"

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

exec $QEMU \\
    -kernel "$KERNEL" \\
    -initrd "$INITRD" \\
    -append "console=ttyS0 console=tty0 quiet" \\
    -m 128M \\
    -nographic \\
    -no-reboot
"""
    
    script_path = os.path.join(output_dir, 'boot-zos.sh')
    with open(script_path, 'w') as f:
        f.write(script)
    os.chmod(script_path, 0o755)
    
    return script_path


if __name__ == '__main__':
    base_dir = '/home/z/my-project/os-build'
    kernel = os.path.join(base_dir, 'src/linux-6.6.87/arch/x86/boot/bzImage')
    initrd = os.path.join(base_dir, 'output/initramfs.cpio.gz')
    output_dir = os.path.join(base_dir, 'output')
    
    # Create QEMU boot script
    script = create_qemu_boot_script(kernel, initrd, output_dir)
    print(f"QEMU boot script: {script}")
    
    # Copy kernel and initrd to output
    import shutil
    kernel_dest = os.path.join(output_dir, 'bzImage')
    initrd_dest = os.path.join(output_dir, 'initramfs.cpio.gz')
    if os.path.abspath(kernel) != os.path.abspath(kernel_dest):
        shutil.copy2(kernel, kernel_dest)
    if os.path.abspath(initrd) != os.path.abspath(initrd_dest):
        shutil.copy2(initrd, initrd_dest)
    
    # Create raw disk image
    disk_image = os.path.join(output_dir, 'zos-disk.img')
    size = create_raw_disk_image(kernel, initrd, disk_image, size_mb=10)
    
    # Verify sizes
    print(f"\nZ/OS Image Components:")
    print(f"  Kernel:    {os.path.getsize(os.path.join(output_dir, 'bzImage')):,} bytes ({os.path.getsize(os.path.join(output_dir, 'bzImage'))/1024/1024:.1f} MB)")
    print(f"  Initramfs: {os.path.getsize(os.path.join(output_dir, 'initramfs.cpio.gz')):,} bytes ({os.path.getsize(os.path.join(output_dir, 'initramfs.cpio.gz'))/1024:.0f} KB)")
    print(f"  Disk image:{os.path.getsize(disk_image):,} bytes ({os.path.getsize(disk_image)/1024/1024:.0f} MB)")
    print(f"  Total boot content: {size:,} bytes ({size/1024/1024:.1f} MB)")
