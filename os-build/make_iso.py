#!/usr/bin/env python3
"""
Create a bootable ISO for SahurOS using ISOLINUX + pycdlib.
Works with VirtualBox, VMware, real hardware, anything that boots ISOs.
"""

import os
import sys
import struct
import pycdlib

def create_bootable_iso(isolinux_dir, kernel_path, initrd_path, output_path):
    """Create an El Torito bootable ISO with ISOLINUX."""
    
    iso = pycdlib.PyCdlib()
    
    # We need to know the sizes upfront for ISOLINUX config
    kernel_size = os.path.getsize(kernel_path)
    initrd_size = os.path.getsize(initrd_path)
    
    # New ISO with Joliet and Rock Ridge support
    iso.new(
        interchange_level=3,
        joliet=True,
        rock_ridge='1.10',
        vol_ident='SAHUROS',
        sys_ident='LINUX',
    )
    
    # Read isolinux files
    isolinux_bin = os.path.join(isolinux_dir, 'isolinux.bin')
    ldlinux_c32 = os.path.join(isolinux_dir, 'ldlinux.c32')
    
    # Create ISOLINUX directory structure on the ISO
    # /isolinux/
    iso.add_directory('/ISOLINUX/', rr_name='ISOLINUX', joliet_path='/isolinux/')
    
    # Add isolinux.bin (bootloader)
    iso.add_file(isolinux_bin, '/ISOLINUX/ISOLINUX.BIN;', rr_name='isolinux.bin', joliet_path='/isolinux/isolinux.bin')
    
    # Add ldlinux.c32 (required by isolinux)
    iso.add_file(ldlinux_c32, '/ISOLINUX/LDLINUX.C32;', rr_name='ldlinux.c32', joliet_path='/isolinux/ldlinux.c32')
    
    # Add the kernel
    iso.add_file(kernel_path, '/ISOLINUX/BZIMAGE;', rr_name='bzImage', joliet_path='/isolinux/bzImage')
    
    # Add the initrd (ISO9660 filename: only A-Z, 0-9, _)
    iso.add_file(initrd_path, '/ISOLINUX/INITRD.GZ;', rr_name='initramfs.cpio.gz', joliet_path='/isolinux/initramfs.cpio.gz')
    
    # Create ISOLINUX.CFG
    isolinux_cfg = b"""DEFAULT sahuros
PROMPT 0
TIMEOUT 30

LABEL sahuros
    KERNEL bzImage
    APPEND initrd=initramfs.cpio.gz console=tty0 console=ttyS0 quiet
"""
    
    # Write isolinux.cfg as a file on the ISO
    cfg_path = '/tmp/isolinux.cfg'
    with open(cfg_path, 'wb') as f:
        f.write(isolinux_cfg)
    
    iso.add_file(cfg_path, '/ISOLINUX/ISOLINUX.CFG;', rr_name='isolinux.cfg', joliet_path='/isolinux/isolinux.cfg')
    
    # Set up El Torito boot using isolinux.bin
    # We need to find the index of the isolinux.bin file
    iso.add_eltorito('/ISOLINUX/ISOLINUX.BIN;', boot_load_size=4, boot_info_table=True)
    iso.write(output_path)
    
    iso.close()
    
    # Clean up temp file
    os.unlink(cfg_path)
    
    return os.path.getsize(output_path)


if __name__ == '__main__':
    base_dir = '/home/z/my-project/os-build'
    iso_tools = '/tmp/iso-extract/usr'
    kernel = os.path.join(base_dir, 'output/bzImage')
    initrd = os.path.join(base_dir, 'output/initramfs.cpio.gz')
    output = os.path.join(base_dir, 'output/SahurOS.iso')
    
    # isolinux files location
    isolinux_bin_dir = os.path.join(iso_tools, 'lib/ISOLINUX')
    ldlinux_dir = os.path.join(iso_tools, 'lib/syslinux/modules/bios')
    
    # Create a combined isolinux dir
    combined_dir = '/tmp/isolinux-combined'
    os.makedirs(combined_dir, exist_ok=True)
    
    import shutil
    shutil.copy2(os.path.join(isolinux_bin_dir, 'isolinux.bin'), combined_dir)
    shutil.copy2(os.path.join(ldlinux_dir, 'ldlinux.c32'), combined_dir)
    
    print("Creating bootable ISO for SahurOS...")
    print(f"  Kernel: {kernel} ({os.path.getsize(kernel):,} bytes)")
    print(f"  Initrd: {initrd} ({os.path.getsize(initrd):,} bytes)")
    
    size = create_bootable_iso(combined_dir, kernel, initrd, output)
    
    print(f"\n  ISO created: {output}")
    print(f"  ISO size: {size:,} bytes ({size/1024/1024:.1f} MB)")
