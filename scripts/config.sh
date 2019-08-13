BUSYBOX_REPO=git://git.busybox.net/busybox
BUSYBOX_TAG=1_30_stable

# apt-get install build-essential zlib1g-dev pkg-config libglib2.0-dev binutils-dev libboost-all-dev autoconf libtool libssl-dev libpixman-1-dev libpython-dev python-pip python-capstone virtualenv

QEMU_REPO=https://git.qemu.org/git/qemu.git
QEMU_TAG=v4.0.0
QEMU_CFG="--target-list=aarch64-softmmu --disable-werror"
QEMU_DISABLE="--disable-libusb --disable-sdl --disable-seccomp --disable-numa --disable-spice --disable-libssh2 --disable-tcmalloc --disable-glusterfs --disable-seccomp --disable-bzip2 --disable-snappy --disable-lzo --disable-usb-redir --disable-libusb --disable-libnfs --disable-tcg-interpreter --disable-debug-tcg --disable-libiscsi --disable-rbd --disable-spice --disable-attr --disable-cap-ng --disable-linux-aio --disable-brlapi --disable-vnc-jpeg --disable-vnc-png --disable-vnc-sasl --disable-rdma --disable-bluez --disable-curl --disable-curses --disable-sdl --disable-gtk --disable-tpm --disable-vte --disable-vnc --disable-xen --disable-opengl --disable-slirp --disable-capstone --disable-capstone"

LINUX_REPO=https://github.com/torvalds/linux.git
LINUX_TAG=v5.0

