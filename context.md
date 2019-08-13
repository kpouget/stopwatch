Context: What is Qemu, para-virtualization and a virtual device?
----------------------------------------------------------------

"QEMU is a generic and open source machine emulator and virtualizer."
It simulates a computer, with different CPUs, motherboards and
peripherals, according to its configuration. Most of the devices mimic
the exact behavior of existing hardware, so that the guest system can
run oblivious to the virtualization layer. These virtual devices may
rely on the underlying physical hardware, to leverage from their
performance or specific processing capabilities. 

The most commonly hardware-accelerated device is the CPU, with the
help of Linux' KVM subsystem. This only works if the virtual CPU is
identical to the physical one. Other devices are emulated in software
inside Qemu: that's what happens when the virtual CPU is not supported
by KVM, then [Qemu TCG] emulates the expected behavior of the
CPU. 

Last, a third possibility is the pass-through of a hardware device to
the guest system. This implies that the host system loses all of its
control of the device and delegates it to the guest system. It has
important trade-offs to consider: usability (can the host live without
this device) vs security (can the guest use this device without safety
issues) vs performance (native device performance in the guest). These
three types of device-sharing allow the usage of the original device
driver inside the guest system.

In this work, we describe another kind of device sharing, so-called
para-virtualized. The guest drivers of these devices may implement any
kind of functionality (networking, graphics, ...), but they would not
be able to operate on any physical hardware. Instead, they rely on a
compatible virtual device running in the hypervisor, that will do the
required operations in host operating system. A common example is the
[network card virtualization], that allows the guest system to
communicate with Qemu, and Qemu in turns uses the host network stack
to send packets to the actual network.

Such para-virtualized devices can implement any kind of feature, based
on the host-guest requirements. Our device will provide very simple
functionalities (a stopwatch time tracker), but the infrastructure
behind it is the base for any more advanced feature. An example of
feature a bit more useful could be the ability to power-off/reboot the
(real) computer. The naive implementation of this capability would
require less than 5 new lines of code in the virtual device!

A last type of para-virtualized device/driver is for the vhost-virtio
infrastructure. They come as a triplet {host kernel module; qemu
virtual device; guest kernel module}, and they focus on the
interactions between the host and guest kernels without trapping down
to Qemu. They are typically used for providing fast and efficient I/O
virtual devices, hence the name VirtIO.

[Qemu TCG]: https://wiki.qemu.org/Documentation/TCG
[network card virtualization]: https://wiki.qemu.org/Documentation/Networking#User_Networking_.28SLIRP.29

Following steps
---------------

This virtual device is just a mock-up, as the stopwatch behavior is
not that useful for the guest system. In real life cases, it will
either communicate with a host kernel module, through `ioctl`s, to
perform controlled accesses to the real hardware; or it will perform
user-land applications of any nature.
