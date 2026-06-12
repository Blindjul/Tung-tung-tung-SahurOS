#!/usr/bin/env python3
"""
Create a bootable ISO for TungOS using ISOLINUX + pycdlib.
Fixes: proper ISO9660 names for ISOLINUX compatibility.
"""

import os
import sys
import pycdlib

def create_bootable_iso(isolinux_dir, kernel_path, initrd_path, output_path):
    iso = pycdlib.PyCdlib()
    
    iso.new(
        interchange_level=1,
        joliet=True,
        rock_ridge='1.10',
        vol_ident='TUNGOS',
        sys_ident='LINUX',
    )
    
    isolinux_bin = os.path.join(isolinux_dir, 'isolinux.bin')
    ldlinux_c32 = os.path.join(isolinux_dir, 'ldlinux.c32')
    
    # /isolinux/ directory
    iso.add_directory('/ISOLINUX/', rr_name='isolinux', joliet_path='/isolinux/')
    
    # Add isolinux.bin
    iso.add_file(isolinux_bin, '/ISOLINUX/ISOLINUX.BIN;1', rr_name='isolinux.bin', joliet_path='/isolinux/isolinux.bin')
    
    # Add ldlinux.c32 - CRITICAL: this must be present for ISOLINUX 6.x
    iso.add_file(ldlinux_c32, '/ISOLINUX/LDLINUX.C32;1', rr_name='ldlinux.c32', joliet_path='/isolinux/ldlinux.c32')
    
    # Add kernel - use simple ISO9660 name
    iso.add_file(kernel_path, '/ISOLINUX/BZIMAGE;1', rr_name='bzImage', joliet_path='/isolinux/bzImage')
    
    # Add initrd
    iso.add_file(initrd_path, '/ISOLINUX/INITRD.GZ;1', rr_name='initramfs.cpio.gz', joliet_path='/isolinux/initramfs.cpio.gz')
    
    # ISOLINUX.CFG - references must match what ISOLINUX sees on ISO9660 fs
    # ISOLINUX reads ISO9660 names (uppercase, with ;1 version)
    # But it also supports Rock Ridge extensions, so lowercase should work
    # The SAFEST approach: use the Rock Ridge names (lowercase)
    isolinux_cfg = b"""DEFAULT tungos
PROMPT 0
TIMEOUT 30

LABEL tungos
    KERNEL bzImage
    APPEND initrd=initramfs.cpio.gz console=tty0 quiet
"""
    
    cfg_path = '/tmp/isolinux.cfg'
    with open(cfg_path, 'wb') as f:
        f.write(isolinux_cfg)
    
    iso.add_file(cfg_path, '/ISOLINUX/ISOLINUX.CFG;1', rr_name='isolinux.cfg', joliet_path='/isolinux/isolinux.cfg')
    
    # El Torito boot
    iso.add_eltorito('/ISOLINUX/ISOLINUX.BIN;1', boot_load_size=4, boot_info_table=True)
    
    iso.write(output_path)
    iso.close()
    
    os.unlink(cfg_path)
    return os.path.getsize(output_path)


if __name__ == '__main__':
    base_dir = '/home/z/my-project/os-build'
    kernel = os.path.join(base_dir, 'output/bzImage')
    initrd = os.path.join(base_dir, 'output/initramfs.cpio.gz')
    output = os.path.join(base_dir, 'output/TungOS.iso')
    
    combined_dir = '/tmp/isolinux-combined'
    os.makedirs(combined_dir, exist_ok=True)
    
    import shutil
    shutil.copy2('/tmp/iso-extract/usr/lib/ISOLINUX/isolinux.bin', combined_dir)
    shutil.copy2('/tmp/iso-extract/usr/lib/syslinux/modules/bios/ldlinux.c32', combined_dir)
    
    print("Creating bootable ISO for TungOS...")
    print(f"  Kernel: {kernel} ({os.path.getsize(kernel):,} bytes)")
    print(f"  Initrd: {initrd} ({os.path.getsize(initrd):,} bytes)")
    
    size = create_bootable_iso(combined_dir, kernel, initrd, output)
    
    print(f"\n  ISO created: {output}")
    print(f"  ISO size: {size:,} bytes ({size/1024/1024:.1f} MB)")
