#!/usr/bin/env python3
"""
Create a bootable ISO for TungOS using ISOLINUX + pycdlib.
Key fix: ISOLINUX config uses ISO9660 filenames (uppercase, 8.3 format).
"""

import os
import shutil
import pycdlib

def create_bootable_iso(isolinux_dir, kernel_path, initrd_path, output_path):
    iso = pycdlib.PyCdlib()
    
    # interchange_level=1 for strict ISO9660 compatibility
    iso.new(
        interchange_level=1,
        joliet=True,
        rock_ridge='1.10',
        vol_ident='TUNGOS',
        sys_ident='LINUX',
    )
    
    isolinux_bin = os.path.join(isolinux_dir, 'isolinux.bin')
    ldlinux_c32 = os.path.join(isolinux_dir, 'ldlinux.c32')
    
    # Create /isolinux/ directory
    iso.add_directory('/ISOLINUX/', rr_name='isolinux', joliet_path='/isolinux/')
    
    # Add files with ISO9660 names (uppercase, 8.3)
    iso.add_file(isolinux_bin, '/ISOLINUX/ISOLINUX.BIN;1', rr_name='isolinux.bin', joliet_path='/isolinux/isolinux.bin')
    iso.add_file(ldlinux_c32, '/ISOLINUX/LDLINUX.C32;1', rr_name='ldlinux.c32', joliet_path='/isolinux/ldlinux.c32')
    iso.add_file(kernel_path, '/ISOLINUX/BZIMAGE;1', rr_name='bzImage', joliet_path='/isolinux/bzImage')
    iso.add_file(initrd_path, '/ISOLINUX/INITRD.GZ;1', rr_name='initramfs.cpio.gz', joliet_path='/isolinux/initramfs.cpio.gz')
    
    # ISOLINUX.CFG - use ISO9660 filenames since ISOLINUX reads ISO9660 directly
    # Also: use separate INITRD directive (more reliable than initrd= in APPEND)
    # Remove "quiet" so we can see boot messages for debugging
    isolinux_cfg = b"""DEFAULT tungos
PROMPT 0
TIMEOUT 30

LABEL tungos
    KERNEL BZIMAGE
    INITRD INITRD.GZ
    APPEND console=tty0 rdinit=/sbin/init
"""
    
    cfg_path = '/tmp/isolinux.cfg'
    with open(cfg_path, 'wb') as f:
        f.write(isolinux_cfg)
    
    iso.add_file(cfg_path, '/ISOLINUX/ISOLINUX.CFG;1', rr_name='isolinux.cfg', joliet_path='/isolinux/isolinux.cfg')
    
    # El Torito boot with boot info table (needed by ISOLINUX)
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
    
    shutil.copy2('/tmp/iso-extract/usr/lib/ISOLINUX/isolinux.bin', combined_dir)
    shutil.copy2('/tmp/iso-extract/usr/lib/syslinux/modules/bios/ldlinux.c32', combined_dir)
    
    print("Creating bootable ISO for TungOS...")
    print(f"  Kernel: {kernel} ({os.path.getsize(kernel):,} bytes)")
    print(f"  Initrd: {initrd} ({os.path.getsize(initrd):,} bytes)")
    
    size = create_bootable_iso(combined_dir, kernel, initrd, output)
    
    print(f"\n  ISO created: {output}")
    print(f"  ISO size: {size:,} bytes ({size/1024/1024:.1f} MB)")
