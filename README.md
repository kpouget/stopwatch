This repository contains a Qemu/Linux aarch64 para-virtualized device
and guest driver. The virtual device exposes 1) a trapping memory
region, 2) a pass-through memory region 3) an IRQ. These 3 resources
are looked up by guest driver through the DTB, and a simple stopwatch
interface is offered to the user-land.

The project is (should be) fully reproducible, so a simple `make all`
will everything:

1. download Qemu, Linux and Busybox,
2. patch them,
3. build Qemu, Linux, and a minimal guest rootfs,
4. execute the demo.

The demonstration runs a stopwatch, with the time counting implemented
in the host (inside Qemu), and the user interface running in the guest
userland.

* What is Qemu, para-virtualization and a virtual device? see the
  [context post](context.md)
* How does this work, actually? see the [explanation post](explained.md)
* If you have any question/issue, let me know!

# Install and run

Install an aarch64 compiler (`aarch64-linux-gnu-gcc`) and its
`glibc`. Then:

```
git clone https://lab.0x972.info/kevin/stopwatch.git
make docker-all # or all
```

Functionalities of the Makefile:

* `all`: runs `prepare patch update run-test-quit`
* `docker-all`: runs `make all` in a docker properly setup

* `prepare` downloads and build Linux/Qemu/Busybox (WARNING: building
the rootfs requires `sudo` privilege, see `scripts/rootfs
build_rootfs`)
* `patch` modifies the tools to include the virtual
device/driver/userland (ie, applies `patches/*.patch` and copies
`guest_fs/*` to the rootfs directory)
* `update` builds Linux, Qemu and the guest rootfs
* `run-test-quit` launches the VM, executes the simple test demo and
terminates the VM

* `module` builds the Linux driver as a module (`stopwatch.ko`)
* `run` runs the VM and launch a shell

* `update_patches` saves Qemu/Linux modifications into `patches/`


# Repository structure

## Static part
(version controlled)

    stopwatch
    └── device
        ├── stopwatch.c
        ├── stopwatch.h
        └── stopwatch_hw-sw.h -> ../driver/stopwatch_hw-sw.h


 The source code of the virtual device (`stopwatch.{c,h}`) and the
 software-hardware interface (`stopwatch_hw-sw.h`).

    stopwatch
    └── driver
        ├── Makefile
        ├── stopwatch.c
        └── stopwatch_hw-sw.h

 The source code of the Linux driver for our virtual device, and the
 software-hardware interface (`stopwatch_hw-sw.h`).

    stopwatch
    └── guest_fs
        ├── init.sh
        ├── network_update.sh
        ├── stopwatch_ctrl.sh
        └── stopwatch_test.sh

 The guest userland scripts to interact with the guest driver:

 * `init.sh`: called at system boot, runs `stopwatch_test.sh` and
   shuts down the VM is requested through Linux commandline
   (`stopwatch=test[_and_quit]`)
 * `stopwatch_ctrl.sh`: the actual interactions with device driver,
   through virtual files
   (`/sys/module/stopwatch/parameters/stopwatch_status` and
   `/sys/kernel/debug/stopwatch/stopwatch`)
 * `stopwatch_test.sh`: simple test script to trigger all the
   functionalities of the driver/device
 * `network_update.sh`: helper script to update the guest files during
   development

 _

    stopwatch
    └── patches
        ├── linux.patch
        └── qemu.patch


Minimal patches for Qemu and Linux repository to include our
device/driver in their build system.

    stopwatch
    ├── scripts
    │   ├── common.sh
    │   ├── config.sh
    │   ├── linux
    │   ├── patches
    │   ├── qemu
    │   ├── rootfs
    │   └── run
    └── Makefile

Helper scripts to prepare/patch/build Qemu, Linux and the VM root
filesystem. See `config.sh` for the location of the sources of these tools.

# Generated part

    stopwatch
    ├── linux
    ├── linux/build
    ├── qemu
    ├── qemu/build
    └── busybox

Source and build directories of Qemu, Linux and Busybox.

    stopwatch
    └── busybox/_install

Install directory of busybox, and directory used to build the guest
root file-system.

    stopwatch
    ├── scripts
    │   └── run
    └── vm
        ├── Image -> ../linux/build/arch/arm64/boot/Image
        ├── qemu-system-aarch64 -> ../qemu/build/aarch64-softmmu/qemu-system-aarch64
        └── rootfs.img

Files used to run the virtual machine: Qemu binary, Linux guest kernel
Image, a root file-system image and a configuration script.

## Troubleshooting

* If you have troubles with the compilation of `busybox` (eg, because
  the aarch64 glibc is missing), try to build it inside my Docker
  image:

```
sudo docker pull kpouget/qemu-aarch64
cd .../stopwatch
sudo docker run -it --user $(id -u):$(id -g) --rm -v $(pwd):$(pwd) -c $(pwd) kpouget/qemu-aarch64 bash
# inside the docker
./scripts/rootfs prepare_busybox
# if everything is OK, quit the docker
./scripts/rootfs build_rootfs # this call the sudo command
# make prepare is finished at this stage
make patch update run-test-quit
```
