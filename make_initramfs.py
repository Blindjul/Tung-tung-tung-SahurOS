#!/usr/bin/env python3
"""
Create a cpio archive (newc format) for initramfs.
This replaces the need for the cpio command.
"""

import os
import struct
import sys
import gzip

def create_cpio_archive(rootfs_dir, output_file):
    """Create a cpio archive in newc format from a directory tree."""
    
    MAGIC = b'070701'  # newc format magic
    entries = []
    
    # Walk the directory tree
    for dirpath, dirnames, filenames in os.walk(rootfs_dir):
        # Add the directory itself
        rel_path = os.path.relpath(dirpath, rootfs_dir)
        if rel_path == '.':
            rel_path = ''
        
        if rel_path:
            entries.append((os.path.join(dirpath, ''), rel_path + '/'))
        
        # Add all files
        for filename in filenames:
            full_path = os.path.join(dirpath, filename)
            rel_file = os.path.relpath(full_path, rootfs_dir)
            entries.append((full_path, rel_file))
        
        # Add empty directories
        for dirname in sorted(dirnames):
            full_dir = os.path.join(dirpath, dirname)
            # Will be added when walked
    
    # Also add the root directory
    entries.insert(0, (None, ''))
    
    # Sort entries by path
    entries.sort(key=lambda x: x[1])
    
    data = bytearray()
    
    for full_path, rel_path in entries:
        # Get file info
        if full_path is None or full_path.endswith('/'):
            # Directory
            mode = 0o755 | 0o40000  # directory + rwx
            nlink = 2
            filesize = 0
            devmajor = 0
            devminor = 0
            body = b''
        elif os.path.islink(full_path):
            # Symlink
            mode = 0o777 | 0o120000  # symlink
            nlink = 1
            filesize = 0
            link_target = os.readlink(full_path)
            body = link_target.encode()
            filesize = len(body)
            devmajor = 0
            devminor = 0
        else:
            # Regular file
            mode = 0o755 | 0o100000  # regular file
            nlink = 1
            try:
                body = open(full_path, 'rb').read()
            except:
                body = b''
            filesize = len(body)
            devmajor = 0
            devminor = 0
        
        namesize = len(rel_path.encode()) + 1  # +1 for null terminator
        ino = hash(rel_path) & 0xFFFFFFFF
        
        # Build header
        header = MAGIC
        header += f'{ino:08X}'.encode()           # inode
        header += f'{mode:08X}'.encode()           # mode
        header += b'00000000'                       # uid
        header += b'00000000'                       # gid
        header += f'{nlink:08X}'.encode()          # nlink
        header += b'00000000'                       # mtime
        header += f'{filesize:08X}'.encode()       # filesize
        header += f'{devmajor:08X}'.encode()       # devmajor
        header += f'{devminor:08X}'.encode()       # devminor
        header += b'00000000'                       # rdevmajor
        header += b'00000000'                       # rdevminor
        header += f'{namesize:08X}'.encode()       # namesize
        header += b'00000000'                       # checksum
        
        # Filename (null-terminated)
        filename_bytes = rel_path.encode() + b'\x00'
        
        # Pad header + filename to 4-byte boundary
        total_header = header + filename_bytes
        pad = (4 - (len(total_header) % 4)) % 4
        total_header += b'\x00' * pad
        
        data.extend(total_header)
        
        # File body
        data.extend(body)
        
        # Pad body to 4-byte boundary
        pad = (4 - (len(body) % 4)) % 4
        data.extend(b'\x00' * pad)
    
    # Add trailer
    trailer = MAGIC
    trailer += b'00000000' * 12  # all zeros
    trailer += b'0000000B'       # namesize = 11
    trailer += b'00000000'       # checksum
    trailer_name = b'TRAILER!!!\x00'
    trailer += trailer_name
    pad = (4 - (len(trailer_name) % 4)) % 4
    trailer += b'\x00' * pad
    data.extend(trailer)
    
    # Pad to 4-byte boundary (block boundary)
    pad = (4 - (len(data) % 4)) % 4
    data.extend(b'\x00' * pad)
    
    # Write the gzip-compressed cpio archive
    with gzip.open(output_file, 'wb') as f:
        f.write(bytes(data))
    
    return len(data)

if __name__ == '__main__':
    rootfs = sys.argv[1] if len(sys.argv) > 1 else '/home/z/my-project/os-build/rootfs'
    output = sys.argv[2] if len(sys.argv) > 2 else '/home/z/my-project/os-build/output/initramfs.cpio.gz'
    
    size = create_cpio_archive(rootfs, output)
    compressed_size = os.path.getsize(output)
    print(f"Initramfs created: {compressed_size} bytes (uncompressed: {size} bytes)")
