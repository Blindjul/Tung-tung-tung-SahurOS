#!/bin/bash
# ╔══════════════════════════════════════════════════════════════════╗
# ║                    Z/OS Build Script                           ║
# ║          Ultra-Lightweight Linux - Version 0.1 "Zephyr"       ║
# ╚══════════════════════════════════════════════════════════════════╝
#
# This script builds Z/OS from source: a ~3MB bootable Linux system.
#
# Prerequisites:
#   - GCC, Make, binutils (ld, as, objcopy, strip)
#   - curl/wget for downloading sources
#   - Python 3 for initramfs creation
#
# Usage:
#   ./build.sh          # Full build from scratch
#   ./build.sh clean    # Clean all build artifacts
#   ./build.sh image    # Just rebuild the disk image
#

set -e

# ═══════════════════════════════════════════════════════════════════
# Configuration
# ═══════════════════════════════════════════════════════════════════
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BASE_DIR="${SCRIPT_DIR}"
SRC_DIR="${BASE_DIR}/src"
ROOTFS="${BASE_DIR}/rootfs"
OUTPUT="${BASE_DIR}/output"
TOOLS="${BASE_DIR}/tools"

KERNEL_VERSION="6.6.87"
BUSYBOX_VERSION="1.36.1"
ELFUTILS_VERSION="0.191"

export PATH="${TOOLS}/bin:${PATH}"
export BISON_PKGDATADIR="${TOOLS}/share/bison"
export C_INCLUDE_PATH="${TOOLS}/include:${C_INCLUDE_PATH}"
export LIBRARY_PATH="${TOOLS}/lib:${LIBRARY_PATH}"

JOBS=$(nproc)

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[1;36m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

banner() {
    echo ""
    echo -e "${CYAN}  ╔══════════════════════════════════════════╗${NC}"
    echo -e "${CYAN}  ║         Z/OS Build System               ║${NC}"
    echo -e "${CYAN}  ║     Ultra-Lightweight Linux v0.1         ║${NC}"
    echo -e "${CYAN}  ╚══════════════════════════════════════════╝${NC}"
    echo ""
}

step() {
    echo -e "${GREEN}[Z/OS]${NC} ${1}"
}

warn() {
    echo -e "${YELLOW}[Z/OS WARNING]${NC} ${1}"
}

error() {
    echo -e "${RED}[Z/OS ERROR]${NC} ${1}"
    exit 1
}

# ═══════════════════════════════════════════════════════════════════
# Clean
# ═══════════════════════════════════════════════════════════════════
do_clean() {
    step "Cleaning build artifacts..."
    rm -rf "${SRC_DIR}/linux-${KERNEL_VERSION}"
    rm -rf "${SRC_DIR}/busybox-${BUSYBOX_VERSION}"
    rm -rf "${SRC_DIR}/elfutils-${ELFUTILS_VERSION}"
    rm -rf "${ROOTFS}"
    rm -rf "${OUTPUT}"
    mkdir -p "${ROOTFS}" "${OUTPUT}"
    step "Clean complete."
}

# ═══════════════════════════════════════════════════════════════════
# Build Tools (flex, bison, bc)
# ═══════════════════════════════════════════════════════════════════
build_tools() {
    step "Checking build tools..."
    mkdir -p "${TOOLS}/bin" "${TOOLS}/src"

    # Flex
    if [ ! -x "${TOOLS}/bin/flex" ]; then
        step "Building flex..."
        cd "${TOOLS}/src"
        [ -f flex-2.6.4.tar.gz ] || curl -sL https://github.com/westes/flex/releases/download/v2.6.4/flex-2.6.4.tar.gz -o flex-2.6.4.tar.gz
        [ -d flex-2.6.4 ] || tar xzf flex-2.6.4.tar.gz
        cd flex-2.6.4
        ./configure --prefix="${TOOLS}" --quiet && make -j${JOBS} -s && make install -s
    fi
    step "  flex: $(${TOOLS}/bin/flex --version | head -1)"

    # Bison
    if [ ! -x "${TOOLS}/bin/bison" ]; then
        step "Building bison..."
        cd "${TOOLS}/src"
        [ -f bison-3.8.2.tar.gz ] || curl -sL https://ftp.gnu.org/gnu/bison/bison-3.8.2.tar.gz -o bison-3.8.2.tar.gz
        [ -d bison-3.8.2 ] || tar xzf bison-3.8.2.tar.gz
        cd bison-3.8.2
        ./configure --prefix="${TOOLS}" --quiet && make -j${JOBS} -s && make install -s
    fi
    step "  bison: $(${TOOLS}/bin/bison --version | head -1)"

    # BC
    if [ ! -x "${TOOLS}/bin/bc" ]; then
        step "Building bc..."
        cd "${TOOLS}/src"
        [ -f bc-1.07.1.tar.gz ] || curl -sL https://ftp.gnu.org/gnu/bc/bc-1.07.1.tar.gz -o bc-1.07.1.tar.gz
        [ -d bc-1.07.1 ] || tar xzf bc-1.07.1.tar.gz
        cd bc-1.07.1
        ./configure --prefix="${TOOLS}" --quiet && make -j${JOBS} -s && make install -s
    fi
    step "  bc: $(${TOOLS}/bin/bc --version | head -1)"

    # libelf (for kernel objtool)
    if [ ! -f "${TOOLS}/include/gelf.h" ]; then
        step "Building elfutils (libelf)..."
        cd "${TOOLS}/src"
        [ -f elfutils-${ELFUTILS_VERSION}.tar.bz2 ] || curl -sL "https://sourceware.org/elfutils/ftp/${ELFUTILS_VERSION}/elfutils-${ELFUTILS_VERSION}.tar.bz2" -o elfutils-${ELFUTILS_VERSION}.tar.bz2
        [ -d elfutils-${ELFUTILS_VERSION} ] || tar xjf elfutils-${ELFUTILS_VERSION}.tar.bz2
        cd elfutils-${ELFUTILS_VERSION}
        ./configure --prefix="${TOOLS}" --quiet 2>/dev/null
        make -j${JOBS} -C lib -s && make -j${JOBS} -C libelf -s && make -C libelf install
    fi
    step "  libelf: $(ls ${TOOLS}/include/gelf.h 2>/dev/null && echo 'OK' || echo 'MISSING')"
}

# ═══════════════════════════════════════════════════════════════════
# Build Kernel
# ═══════════════════════════════════════════════════════════════════
build_kernel() {
    step "Building Linux kernel ${KERNEL_VERSION}..."
    cd "${SRC_DIR}"

    # Download
    [ -f linux-${KERNEL_VERSION}.tar.xz ] || curl -sL "https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-${KERNEL_VERSION}.tar.xz" -o linux-${KERNEL_VERSION}.tar.xz
    [ -d linux-${KERNEL_VERSION} ] || tar xf linux-${KERNEL_VERSION}.tar.xz

    cd linux-${KERNEL_VERSION}

    # Configure: start from tinyconfig and add essentials
    make tinyconfig

    # Core features
    ./scripts/config --enable 64BIT
    ./scripts/config --enable BLK_DEV_INITRD
    ./scripts/config --enable RD_GZIP
    ./scripts/config --enable TTY
    ./scripts/config --enable VT
    ./scripts/config --enable VT_CONSOLE
    ./scripts/config --enable SERIAL_8250
    ./scripts/config --enable SERIAL_8250_CONSOLE
    ./scripts/config --enable PRINTK
    ./scripts/config --enable PROC_FS
    ./scripts/config --enable SYSFS
    ./scripts/config --enable DEVTMPFS
    ./scripts/config --enable DEVTMPFS_MOUNT
    ./scripts/config --enable BLOCK
    ./scripts/config --enable BINFMT_ELF
    ./scripts/config --enable BINFMT_SCRIPT
    ./scripts/config --enable SIGNALFD
    ./scripts/config --enable INOTIFY_USER
    ./scripts/config --enable TMPFS
    ./scripts/config --enable PROC_SYSCTL
    ./scripts/config --enable PCI
    ./scripts/config --enable PCI_GOANY
    ./scripts/config --enable FUTEX
    ./scripts/config --enable EPOLL
    ./scripts/config --enable TIMERFD
    ./scripts/config --enable SHMEM
    ./scripts/config --enable AIO
    ./scripts/config --enable SMP
    ./scripts/config --enable KERNEL_GZIP
    ./scripts/config --enable MULTIUSER
    ./scripts/config --enable NET
    ./scripts/config --enable INET
    ./scripts/config --enable PACKET
    ./scripts/config --enable UNIX
    ./scripts/config --enable EXT4_FS
    ./scripts/config --enable EXT4_USE_FOR_EXT2
    ./scripts/config --enable JBD2
    ./scripts/config --enable CRC16
    ./scripts/config --enable FS_MBCACHE
    ./scripts/config --enable PIPE
    ./scripts/config --enable FHANDLE
    ./scripts/config --enable ADVISE_SYSCALLS
    ./scripts/config --enable MMU
    ./scripts/config --enable STACK_VALIDATION

    # Resolve new config options
    yes "" | make oldconfig

    # Build
    make -j${JOBS} HOSTCFLAGS="-I${TOOLS}/include" HOSTLDFLAGS="-L${TOOLS}/lib"

    # Copy kernel
    cp arch/x86/boot/bzImage "${OUTPUT}/bzImage"
    step "  Kernel size: $(du -h ${OUTPUT}/bzImage | cut -f1)"
}

# ═══════════════════════════════════════════════════════════════════
# Build BusyBox
# ═══════════════════════════════════════════════════════════════════
build_busybox() {
    step "Building BusyBox ${BUSYBOX_VERSION}..."
    cd "${SRC_DIR}"

    # Download
    [ -f busybox-${BUSYBOX_VERSION}.tar.bz2 ] || curl -sL "https://busybox.net/downloads/busybox-${BUSYBOX_VERSION}.tar.bz2" -o busybox-${BUSYBOX_VERSION}.tar.bz2
    [ -d busybox-${BUSYBOX_VERSION} ] || tar xf busybox-${BUSYBOX_VERSION}.tar.bz2

    cd busybox-${BUSYBOX_VERSION}

    # Configure: allnoconfig + essential applets
    make allnoconfig

    # Static linking
    sed -i 's/# CONFIG_STATIC is not set/CONFIG_STATIC=y/' .config

    # Shell
    sed -i 's/# CONFIG_ASH is not set/CONFIG_ASH=y/' .config
    sed -i 's/# CONFIG_SH_IS_ASH is not set/CONFIG_SH_IS_ASH=y/' .config
    sed -i 's/# CONFIG_BASH_IS_ASH is not set/CONFIG_BASH_IS_ASH=y/' .config
    sed -i 's/# CONFIG_ASH_ECHO is not set/CONFIG_ASH_ECHO=y/' .config
    sed -i 's/# CONFIG_ASH_PRINTF is not set/CONFIG_ASH_PRINTF=y/' .config
    sed -i 's/# CONFIG_ASH_TEST is not set/CONFIG_ASH_TEST=y/' .config
    sed -i 's/# CONFIG_ASH_HELP is not set/CONFIG_ASH_HELP=y/' .config
    sed -i 's/# CONFIG_ASH_ALIAS is not set/CONFIG_ASH_ALIAS=y/' .config
    sed -i 's/# CONFIG_ASH_JOB_CONTROL is not set/CONFIG_ASH_JOB_CONTROL=y/' .config
    sed -i 's/# CONFIG_ASH_CMDCMD is not set/CONFIG_ASH_CMDCMD=y/' .config
    sed -i 's/# CONFIG_ASH_BASH_COMPAT is not set/CONFIG_ASH_BASH_COMPAT=y/' .config
    sed -i 's/# CONFIG_ASH_OPTIMIZE_FOR_SIZE is not set/CONFIG_ASH_OPTIMIZE_FOR_SIZE=y/' .config

    # Init
    sed -i 's/# CONFIG_INIT is not set/CONFIG_INIT=y/' .config
    sed -i 's/# CONFIG_FEATURE_INIT_MODIFY_CMDLINE is not set/CONFIG_FEATURE_INIT_MODIFY_CMDLINE=y/' .config

    # Coreutils
    for app in CAT ECHO LS CP MV RM MKDIR RMDIR PWD CD TRUE FALSE SLEEP DATE UNAME ID WHOAMI \
               CLEAR HEAD TAIL WC TOUCH MKNOD CHMOD CHOWN LN BASENAME DIRNAME MKTEMP TEE TEST \
               CHROOT DD DF DU STAT TR CUT SORT UNIQ; do
        sed -i "s/# CONFIG_${app} is not set/CONFIG_${app}=y/" .config
    done

    # Process/System
    for app in PS KILL FREE UPTIME REBOOT POWEROFF; do
        sed -i "s/# CONFIG_${app} is not set/CONFIG_${app}=y/" .config
    done

    # Utilities
    for app in DMESG MOUNT UMOUNT GETTY HOSTNAME SWITCH_ROOT GREP FIND VI GZIP GUNZIP TAR CPIO; do
        sed -i "s/# CONFIG_${app} is not set/CONFIG_${app}=y/" .config
    done

    # Feature flags
    sed -i 's/# CONFIG_FEATURE_DEVPTS is not set/CONFIG_FEATURE_DEVPTS=y/' .config
    sed -i 's/# CONFIG_FEATURE_SH_STANDALONE is not set/CONFIG_FEATURE_SH_STANDALONE=y/' .config
    sed -i 's/# CONFIG_FEATURE_PREFER_APPLETS is not set/CONFIG_FEATURE_PREFER_APPLETS=y/' .config
    sed -i 's/# CONFIG_SHOW_USAGE is not set/CONFIG_SHOW_USAGE=y/' .config
    sed -i 's/# CONFIG_FEATURE_COMPRESS_USAGE is not set/CONFIG_FEATURE_COMPRESS_USAGE=y/' .config
    sed -i 's/# CONFIG_LFS is not set/CONFIG_LFS=y/' .config
    sed -i 's/# CONFIG_INSTALL_APPLET_SYMLINKS is not set/CONFIG_INSTALL_APPLET_SYMLINKS=y/' .config
    sed -i 's/# CONFIG_INSTALL_NO_USR is not set/CONFIG_INSTALL_NO_USR=y/' .config
    sed -i 's/# CONFIG_LONG_OPTS is not set/CONFIG_LONG_OPTS=y/' .config

    # Resolve new options
    yes "" | make oldconfig

    # Build
    make -j${JOBS}
    step "  BusyBox size: $(du -h busybox | cut -f1)"
}

# ═══════════════════════════════════════════════════════════════════
# Build Rootfs
# ═══════════════════════════════════════════════════════════════════
build_rootfs() {
    step "Building root filesystem..."

    # Create directory structure
    rm -rf "${ROOTFS}"
    mkdir -p "${ROOTFS}"/{bin,sbin,etc/init.d,proc,sys,dev,tmp,var,run,usr/bin,usr/sbin,home,root,mnt}

    # Install BusyBox
    cd "${SRC_DIR}/busybox-${BUSYBOX_VERSION}"
    make CONFIG_PREFIX="${ROOTFS}" install -s

    # Create init scripts
    cat > "${ROOTFS}/etc/init.d/rcS" << 'INITEOF'
#!/bin/sh
echo ""
echo "  ╔══════════════════════════════════════════╗"
echo "  ║                                          ║"
echo "  ║         ██╗  ████████╗  ██╗              ║"
echo "  ║         ╚██╗╚══██╔══╝  ██║              ║"
echo "  ║          ╚██╗  ██║████╗██║              ║"
echo "  ║          ██╔╝  ██║╚██╔╝██║              ║"
echo "  ║         ██╔╝   ██║ ╚═╝ ██║              ║"
echo "  ║         ╚═╝    ╚═╝     ╚═╝              ║"
echo "  ║                                          ║"
echo "  ║     Z/OS  -  Ultra-Lightweight Linux     ║"
echo "  ║          Version 0.1 \"Zephyr\"            ║"
echo "  ║                                          ║"
echo "  ╚══════════════════════════════════════════╝"
echo ""

echo "[Z/OS] Mounting virtual filesystems..."
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
mkdir -p /dev/pts
mount -t devpts devpts /dev/pts
mount -t tmpfs tmpfs /tmp
mount -t tmpfs tmpfs /run

hostname z-os
echo "z-os" > /etc/hostname

[ -e /dev/console ] || mknod /dev/console c 5 1
[ -e /dev/null ] || mknod /dev/null c 1 3
[ -e /dev/tty ] || mknod /dev/tty c 5 0
[ -e /dev/zero ] || mknod /dev/zero c 1 5

export PATH=/bin:/sbin:/usr/bin:/usr/sbin
export HOME=/root
export TERM=linux
export PS1='\[\033[1;36m\]z/os\[\033[0m\]:\[\033[1;34m\]\w\[\033[0m\]\$ '

echo "[Z/OS] System ready."
echo "[Z/OS] Kernel: $(uname -r)"
echo "[Z/OS] Arch:   $(uname -m)"
echo "[Z/OS] RAM:    $(free | head -2 | tail -1 | awk '{print $2}') KB"
echo ""
INITEOF
    chmod +x "${ROOTFS}/etc/init.d/rcS"

    # Inittab
    cat > "${ROOTFS}/etc/inittab" << 'INITEOF'
::sysinit:/etc/init.d/rcS
::respawn:-/bin/sh
::ctrlaltdel:/sbin/reboot
::shutdown:/bin/umount -a -r
INITEOF

    # Profile
    cat > "${ROOTFS}/etc/profile" << 'PROFILEEOF'
export PATH=/bin:/sbin:/usr/bin:/usr/sbin
export HOME=/root
export TERM=linux
export PS1='\[\033[1;36m\]z/os\[\033[0m\]:\[\033[1;34m\]\w\[\033[0m\]\$ '
alias ll='ls -la'
alias la='ls -a'
alias cls='clear'
echo ""
echo "  Welcome to Z/OS! Type 'help' for available commands."
echo "  Use 'poweroff' or 'reboot' when done."
echo ""
PROFILEEOF

    # /etc files
    echo "root:x:0:0:root:/root:/bin/sh" > "${ROOTFS}/etc/passwd"
    echo "nobody:x:65534:65534:nobody:/nonexistent:/bin/false" >> "${ROOTFS}/etc/passwd"
    echo "root:x:0:" > "${ROOTFS}/etc/group"
    echo "nobody:x:65534:" >> "${ROOTFS}/etc/group"
    echo "127.0.0.1       localhost z-os" > "${ROOTFS}/etc/hosts"
    echo "::1             localhost" >> "${ROOTFS}/etc/hosts"
    echo "z-os" > "${ROOTFS}/etc/hostname"

    # Help command
    cat > "${ROOTFS}/bin/help" << 'HELPEOF'
#!/bin/sh
echo ""
echo "  Z/OS Available Commands:"
echo "  ═════════════════════════"
echo ""
echo "  File:     ls, cp, mv, rm, mkdir, rmdir, ln, chmod, chown, cat, touch, df, du, tar, gzip, gunzip"
echo "  Text:     echo, head, tail, wc, grep, find, cut, sort, basename, dirname, tee"
echo "  System:   ps, kill, free, uptime, dmesg, mount, umount, hostname, uname, reboot, poweroff"
echo "  Shell:    ash (built-in), cd, pwd, export, alias, source"
echo "  Other:    date, sleep, id, whoami, vi, clear, reset, cpio, dd, test, true, false"
echo ""
echo "  Shortcuts: ll='ls -la', la='ls -a', cls='clear'"
echo ""
HELPEOF
    chmod +x "${ROOTFS}/bin/help"

    # Sysinfo command
    cat > "${ROOTFS}/bin/sysinfo" << 'SYSINFOEOF'
#!/bin/sh
echo ""
echo "  ╔═══════════════════════════════════════╗"
echo "  ║         Z/OS System Information       ║"
echo "  ╚═══════════════════════════════════════╝"
echo ""
echo "  OS:       Z/OS v0.1 \"Zephyr\""
echo "  Kernel:   $(uname -r)"
echo "  Arch:     $(uname -m)"
echo "  Hostname: $(hostname)"
echo "  Uptime:   $(uptime)"
echo "  Memory:   $(free | head -2 | tail -1 | awk '{print $2}') KB total"
echo ""
SYSINFOEOF
    chmod +x "${ROOTFS}/bin/sysinfo"

    step "  Rootfs created with $(ls ${ROOTFS}/bin/ | wc -l) commands"
}

# ═══════════════════════════════════════════════════════════════════
# Build Initramfs
# ═══════════════════════════════════════════════════════════════════
build_initramfs() {
    step "Creating initramfs..."
    python3 "${BASE_DIR}/make_initramfs.py" "${ROOTFS}" "${OUTPUT}/initramfs.cpio.gz"
    step "  Initramfs size: $(du -h ${OUTPUT}/initramfs.cpio.gz | cut -f1)"
}

# ═══════════════════════════════════════════════════════════════════
# Build Disk Image
# ═══════════════════════════════════════════════════════════════════
build_image() {
    step "Creating disk image..."
    python3 "${BASE_DIR}/make_image.py"

    # Create boot script
    cat > "${OUTPUT}/boot-zos.sh" << 'BOOTEOF'
#!/bin/sh
# Z/OS - QEMU Launch Script
KERNEL="bzImage"
INITRD="initramfs.cpio.gz"

echo "  ╔══════════════════════════════════════════╗"
echo "  ║          Booting Z/OS with QEMU          ║"
echo "  ╚══════════════════════════════════════════╝"
echo ""

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
BOOTEOF
    chmod +x "${OUTPUT}/boot-zos.sh"

    # Summary
    echo ""
    echo -e "${CYAN}  ╔══════════════════════════════════════════╗${NC}"
    echo -e "${CYAN}  ║       Z/OS Build Complete!               ║${NC}"
    echo -e "${CYAN}  ╚══════════════════════════════════════════╝${NC}"
    echo ""
    echo "  Kernel:     $(du -h ${OUTPUT}/bzImage | cut -f1)"
    echo "  Initramfs:  $(du -h ${OUTPUT}/initramfs.cpio.gz | cut -f1)"
    echo "  Disk image: $(du -h ${OUTPUT}/zos-disk.img | cut -f1)"
    echo ""
    echo "  To boot with QEMU:"
    echo "    cd ${OUTPUT}"
    echo "    ./boot-zos.sh"
    echo ""
    echo "  Or manually:"
    echo "    qemu-system-x86_64 -kernel bzImage -initrd initramfs.cpio.gz -append 'console=ttyS0' -m 128M -nographic"
    echo ""
}

# ═══════════════════════════════════════════════════════════════════
# Main
# ═══════════════════════════════════════════════════════════════════
case "${1:-build}" in
    clean)
        do_clean
        ;;
    tools)
        build_tools
        ;;
    kernel)
        mkdir -p "${OUTPUT}"
        build_kernel
        ;;
    busybox)
        mkdir -p "${OUTPUT}"
        build_busybox
        ;;
    rootfs)
        build_rootfs
        ;;
    initramfs)
        mkdir -p "${OUTPUT}"
        build_rootfs
        build_initramfs
        ;;
    image)
        build_image
        ;;
    build|"")
        banner
        mkdir -p "${SRC_DIR}" "${OUTPUT}"
        build_tools
        build_kernel
        build_busybox
        build_rootfs
        build_initramfs
        build_image
        ;;
    *)
        echo "Usage: $0 {build|clean|tools|kernel|busybox|rootfs|initramfs|image}"
        exit 1
        ;;
esac
