#!/usr/bin/env python3
"""
Create a cpio archive (newc format) for initramfs.
Strictly follows the newc specification to avoid kernel unpacking errors.
"""

import os
import stat
import struct
import gzip

CPIO_MAGIC = b'070701'

def pad4(size):
    """Return number of padding bytes needed to align to 4 bytes."""
    return (4 - (size % 4)) % 4

def make_cpio_header(ino, mode, uid, gid, nlink, mtime, filesize,
                     devmajor, devminor, rdevmajor, rdevminor, namesize):
    """Build a newc format cpio header."""
    hdr = CPIO_MAGIC
    hdr += f'{ino:08X}'.encode()
    hdr += f'{mode:08X}'.encode()
    hdr += f'{uid:08X}'.encode()
    hdr += f'{gid:08X}'.encode()
    hdr += f'{nlink:08X}'.encode()
    hdr += f'{mtime:08X}'.encode()
    hdr += f'{filesize:08X}'.encode()
    hdr += f'{devmajor:08X}'.encode()
    hdr += f'{devminor:08X}'.encode()
    hdr += f'{rdevmajor:08X}'.encode()
    hdr += f'{rdevminor:08X}'.encode()
    hdr += f'{namesize:08X}'.encode()
    hdr += b'00000000'  # checksum (0 = ignore)
    return hdr

def create_cpio_archive(rootfs_dir, output_file):
    """Create a cpio archive in newc format from a directory tree."""
    
    entries = []
    
    # Walk the directory tree, collecting entries
    for dirpath, dirnames, filenames in os.walk(rootfs_dir):
        # Sort dirnames for deterministic output
        dirnames.sort()
        
        # Add the directory entry itself
        rel_dir = os.path.relpath(dirpath, rootfs_dir)
        if rel_dir == '.':
            rel_dir = ''
        
        # Add directory entry (skip root - we'll add it separately)
        if rel_dir:
            entries.append(('dir', os.path.join(dirpath, ''), rel_dir + '/'))
        
        # Add files and symlinks
        for filename in sorted(filenames):
            full_path = os.path.join(dirpath, filename)
            rel_path = os.path.relpath(full_path, rootfs_dir)
            
            if os.path.islink(full_path):
                entries.append(('link', full_path, rel_path))
            else:
                entries.append(('file', full_path, rel_path))
    
    # Prepend root directory
    entries.insert(0, ('dir', None, ''))
    
    data = bytearray()
    ino_counter = 1
    
    for entry_type, full_path, rel_path in entries:
        # Determine file properties
        if entry_type == 'dir':
            mode = 0o755 | stat.S_IFDIR
            nlink = 2  # directories have at least 2 (. and ..)
            filesize = 0
            body = b''
            rdevmajor = 0
            rdevminor = 0
            mtime = 0
        elif entry_type == 'link':
            mode = 0o777 | stat.S_IFLNK
            nlink = 1
            link_target = os.readlink(full_path)
            body = link_target.encode()
            filesize = len(body)
            rdevmajor = 0
            rdevminor = 0
            mtime = 0
        else:  # file
            st = os.stat(full_path)
            mode = st.st_mode
            nlink = st.st_nlink
            mtime = int(st.st_mtime)
            with open(full_path, 'rb') as f:
                body = f.read()
            filesize = len(body)
            rdevmajor = 0
            rdevminor = 0
        
        # Filename: null-terminated
        filename_bytes = rel_path.encode() + b'\x00'
        namesize = len(filename_bytes)
        
        ino = ino_counter
        ino_counter += 1
        
        # Build header (110 bytes exactly)
        header = make_cpio_header(
            ino=ino,
            mode=mode,
            uid=0,
            gid=0,
            nlink=nlink,
            mtime=mtime,
            filesize=filesize,
            devmajor=0,
            devminor=0,
            rdevmajor=rdevmajor,
            rdevminor=rdevminor,
            namesize=namesize,
        )
        
        assert len(header) == 110, f"Header length is {len(header)}, expected 110"
        
        # Write header + filename + padding
        data.extend(header)
        data.extend(filename_bytes)
        # Pad header+filename to 4-byte boundary
        hdr_plus_name_len = 110 + namesize
        data.extend(b'\x00' * pad4(hdr_plus_name_len))
        
        # Write file data + padding
        if filesize > 0:
            data.extend(body)
            data.extend(b'\x00' * pad4(filesize))
    
    # Write trailer entry
    trailer_name = b'TRAILER!!!\x00'
    trailer_header = make_cpio_header(
        ino=0, mode=0, uid=0, gid=0, nlink=1,
        mtime=0, filesize=0, devmajor=0, devminor=0,
        rdevmajor=0, rdevminor=0, namesize=len(trailer_name),
    )
    data.extend(trailer_header)
    data.extend(trailer_name)
    # Pad trailer to 4-byte boundary
    hdr_plus_name_len = 110 + len(trailer_name)
    data.extend(b'\x00' * pad4(hdr_plus_name_len))
    
    # Pad entire archive to 4-byte boundary
    data.extend(b'\x00' * pad4(len(data)))
    
    # Write gzip-compressed
    with gzip.open(output_file, 'wb') as f:
        f.write(bytes(data))
    
    return len(data), os.path.getsize(output_file)


if __name__ == '__main__':
    import sys
    rootfs = sys.argv[1] if len(sys.argv) > 1 else '/home/z/my-project/os-build/rootfs'
    output = sys.argv[2] if len(sys.argv) > 2 else '/home/z/my-project/os-build/output/initramfs.cpio.gz'
    
    raw_size, compressed_size = create_cpio_archive(rootfs, output)
    print(f"Initramfs created: {compressed_size:,} bytes compressed, {raw_size:,} bytes raw")
