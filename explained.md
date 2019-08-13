Qemu Virtual Device Dissection
==============================

In this article, we dissect the implementation of a basic virtual
device for Qemu/aarch64 virtual machines. For the illustration, this
device offers simple stopwatch time-tracking capabilities. A guest
virtual device interacts with it in the guest kernel, and it exposes a
simple file-based interface to the user-land.

The [project](README.md) is (should be) fully reproducible, so a simple `make all`
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
* If you have any question/issue, let me know!

In the following, we will go step by step through the different pieces
of the virtual device, more or less in their execution order.


Defining the device to Qemu
---------------------------

First, we must inform Qemu that a new kind of virtual device is
available:

```
//~ include/hw/misc/stopwatch.h

#define TYPE_STOPWATCH            "stopwatch"

struct StopWatchState {
    /*< private >*/
    SysBusDevice dev;

    /*< properties >*/
    bool start_at_boot;

    /*< public >*/
    ...

    /*< internal state >*/
    ...
}


//~ hw/misc/stopwatch.c

static Property stopwatch_properties[] = {
    DEFINE_PROP_BOOL("start_at_boot", struct StopWatchState, start_at_boot, true),
    DEFINE_PROP_END_OF_LIST(),
};

static void stopwatch_realize(DeviceState *dev, Error **errp);
static void stopwatch_unrealize(DeviceState *dev, Error **errp);

static void stopwatch_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->props = stopwatch_properties;
    dc->realize = stopwatch_realize;
    dc->unrealize = stopwatch_unrealize;
    dc->user_creatable = true;

    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo stopwatch_info = {
    .name           = TYPE_STOPWATCH,
    .parent         = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(struct StopWatchState),
    .class_init     = stopwatch_class_init,
    .class_size     = sizeof(struct StopWatchDeviceClass),
};

static void stopwatch_register_types(void)
{
    type_register_static(&stopwatch_info);
}

type_init(stopwatch_register_types);

```

Functions `realize` and `unrealize` are the constructor/destructor of
the virtual device object. The `properties` array allows passing
command-line values to our device.

At last, to allow the device to be dynamically instantiated from the
command line, we must allow its binding to the `virt`
platform. Without it, Qemu launch fails with the error message "Option
'-device stopwatch' cannot be handled by this machine".

```
//~ hw/arm/virt.c

machine_class_allow_dynamic_sysbus_dev(mc, TYPE_STOPWATCH);
```


After the execution of these lines, we will be able to launch Qemu
with our device with this command-line option:

    -device stopwatch,start_at_boot=true


Exposing the device virtual interface
-------------------------------------

Once our device has been registered in the virtual machine platform,
we must define its interface with the system.

For our stopwatch illustration, we exposes three resources that
highlight three different ways to interact with the guest system:

1. a passthrough memory-mapped IO (MMIO) region
2. a trapping MMIO region
3. an IRQ

### Passthrough memory-mapped IO region

This device memory region is a buffer that is simply shared by Qemu
and the guest system. They can both read and write into this
buffer. Mind, though, that this is not RAM memory from the guest point
of view, but device MMIO. It requires specific Linux functions to read
and write from it, as we'll see later.

```
//~ include/hw/misc/stopwatch.h

struct StopWatch_mem;
struct StopWatchState {
    ...
    MemoryRegion mem;
    struct StopWatch_mem *mem_ptr;
    ...
}

//~ hw/misc/stopwatch.c

#define STOPWATCH_IO_MEM_SIZE (sizeof(struct StopWatch_mem))
static void stopwatch_realize(DeviceState *dev, Error **errp)
{
    struct StopWatchState *s = STOPWATCH(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    s->mem_ptr = mmap(0, STOPWATCH_IO_MEM_SIZE, PROT_READ|PROT_WRITE,
                      MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (s->mem_ptr == MAP_FAILED) {
        error_setg_errno(errp, errno, "Unable to allocate the stopwatch memory");
        return;
    }

    memory_region_init_ram_ptr(&s->mem, OBJECT(s), TYPE_STOPWATCH"-mem",
                               STOPWATCH_IO_MEM_SIZE, s->mem_ptr);
    sysbus_init_mmio(sbd, &s->mem);
    ...
}
```

### Trapping memory-mapped IO region

From the guest point of view, this memory region is very similar to
the passthrough MMIO. However, they will behave differently in Qemu:
this one will trap and let us execute code on read and write
operations.

```
//~ include/hw/misc/stopwatch.h

struct StopWatch_regs;
struct StopWatchState {
    ...
    MemoryRegion regs;
    ...
}

//~ hw/misc/stopwatch.c

static void stopwatch_io_write(void *opaque, hwaddr offset, uint64_t command,
                               unsigned size);
static uint64_t stopwatch_io_read(void *opaque, hwaddr offset,
                                  unsigned size);

static const MemoryRegionOps stopwatch_regs_ops = {
    .write = stopwatch_io_write,
    .read = stopwatch_io_read,
    .impl = {
        .min_access_size = 8,
        .max_access_size = 8,
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

#define STOPWATCH_IO_REGS_SIZE (sizeof(struct StopWatch_regs))
static void stopwatch_realize(DeviceState *dev, Error **errp)
{
    struct StopWatchState *s = STOPWATCH(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->regs, OBJECT(s), &stopwatch_regs_ops, s,
                          TYPE_STOPWATCH"-regs",
                          STOPWATCH_IO_REGS_SIZE);
    sysbus_init_mmio(sbd, &s->regs);
}
```

When the guest system will read and write into this memory region, the
execution context will be switched from the virtual system to Qemu,
and the relevant callback will be executed. It receives as arguments
the parameter of the operation, and returns (for the `read`) the
value that the guest should read.

### IRQ notification


The last device resource is a virtual IRQ line, for notifying the
guest that a particular event occurred.

```
//~ include/hw/misc/stopwatch.h

struct StopWatchState {
    /*< private >*/
    SysBusDevice dev;

    /*< public >*/
    qemu_irq timeout_irq;
    ...
}

//~ hw/misc/stopwatch.c

static void stopwatch_realize(DeviceState *dev, Error **errp)
{
    struct StopWatchState *s = STOPWATCH(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    sysbus_init_irq(sbd, &s->timeout_irq);
    ...
}
```

And to trigger the IRQ, and turn it of (more on it later, as part of
the stopwatch behavior implementation):

```
    struct StopWatchState *s;
    ...
    qemu_set_irq(s->timeout_irq, 1); // set the IRQ to HIGH (activate it)
    qemu_set_irq(s->timeout_irq, 0); // set the IRQ to LOW (deactivate it)
```

At this point, we have fully structured virtual device. But, before
implementing its actual behavior, we must be able to inform the guest
system about this device. In the ARMv8 architecture, this information
is passed via the flat device tree (FDT).

Exposing the device to the guest system
---------------------------------------

Once we have defined our device in Qemu, ie at the virtual hardware
level, we must inform the guest system about its presence, and tell it
were the different resources are located in memory. This is the
purpose of the Flat Device Tree (FDT).

For real platforms, the FDT is provided by the machine vendor, and/or
compiled as part of Linux kernel compilation for a particular
board. In our virtual environment, and in particular for Qemu `virt`
platform, the device tree is dynamically generated based on the
command line options. An alternative way to operate is to pass Qemu a
pre-built device tree, and let Qemu build the virtual machine and
devices based on this specification.

### Registering our device tree entry generator

```
\\~ hw/arm/sysbus-fdt.c

int add_stopwatch_fdt_node(SysBusDevice *sbdev, void *opaque);
/* list of supported dynamic sysbus bindings */
static const BindingEntry bindings[] = {
    ...
    TYPE_BINDING(TYPE_STOPWATCH, add_stopwatch_fdt_node),
    ...
}
```

This will tell Qemu that, if the stopwatch device is instantiated, it
should run `add_stopwatch_fdt_node` functions to extend accordingly
the device tree.

### Stopwatch device tree entry

The device tree entry of our device should look like this:

```
stopwatch@BASE_ADDR {
	compatible = "stopwatch";
	reg = <0xMEM_ADDR 0xMEM_SZ 0xREGS_ADDR 0xREGS_SZ>;
	interrupts = <0xIRQ_IDX 0x70 0x04>;
};
```

* `stopwatch@BASE_ADDR` is a unique name,
* the `compatible` string allows the lookup of the compatible driver
* the `reg` property (list of addr,size) indicates the location of the
  MMIO regions,
* the `interrupts` property (list if idx,type,level) indicate the IRQ
  lines to use and the triggering level.

Here is the entry actually generated for this illustration
(`./scripts/run dump-dtb`):

```
stopwatch@0 {
	compatible = "stopwatch";
	interrupts = <0x00 0x70 0x04>;
	reg = <0x00 0x88 0x90 0x10>;
};
```

### Device tree entry generation

Now let us look step by step how this device tree entry was generated

Note that in this repository, function `add_stopwatch_fdt_node` is
located at the end of `hw/misc/stopwatch.c`, but this is only for
keeping the code out of Qemu repository. It rather belongs to
`hw/arm/sysbus-fdt.c`.

```
\\~ hw/misc/stopwatch.c

int add_stopwatch_fdt_node(SysBusDevice *sbdev, void *opaque) {
```

1. Node name and compatible string (`<parent>/TYPE@BASE_ADDR { ... }`):

```
    PlatformBusFDTData *data = opaque;
    PlatformBusDevice *pbus = data->pbus;
    void *fdt = data->fdt;
    const char *parent_node = data->pbus_node_name;

    uint64_t mmio_base;
    char *nodename;

    mmio_base = platform_bus_get_mmio_addr(pbus, sbdev, 0);
    nodename = g_strdup_printf("%s/%s@%" PRIx64, parent_node,
                               TYPE_STOPWATCH, mmio_base);
    qemu_fdt_add_subnode(fdt, nodename);

    qemu_fdt_setprop(fdt, nodename, "compatible", STOPWATCH_COMPAT_STR,
                     sizeof(STOPWATCH_COMPAT_STR));
```

2. The region property (`reg = <addr1 sz1> addr2 sz2`)

```
    struct StopWatchState *stopwatch_state = STOPWATCH(sbdev);
    uint32_t  *reg_attr;
    const int NB_MEM_REGION = 2;

    reg_attr = g_new(uint32_t, NB_MEM_REGION*2); /* Tree memory regions */

    /* io memory region */
    mmio_base = platform_bus_get_mmio_addr(pbus, sbdev, 0);
    reg_attr[0] = cpu_to_be32(mmio_base);
    reg_attr[1] = cpu_to_be32(memory_region_size(&stopwatch_state->mem));

    /* regs memory region */
    mmio_base = platform_bus_get_mmio_addr(pbus, sbdev, 1);
    reg_attr[2] = cpu_to_be32(mmio_base);
    reg_attr[3] = cpu_to_be32(memory_region_size(&stopwatch_state->regs));

    /* main 'reg' node */
    ret = qemu_fdt_setprop(fdt, nodename, "reg", reg_attr,
                           NB_MEM_REGION * 2 * sizeof(uint32_t));
```

3. The IRQ property

```
    uint32_t *irq_attr;
    uint64_t irq_number;

#define STOPWATCH_NB_IRQ 1
#define STOPWATCH_TIMEOUT_IRQ_IDX 0

    irq_attr = g_new(uint32_t, STOPWATCH_NB_IRQ * 3);
    irq_number = platform_bus_get_irqn(pbus, sbdev ,STOPWATCH_TIMEOUT_IRQ_IDX)
        + data->irq_start;

    irq_attr[3 * STOPWATCH_TIMEOUT_IRQ_IDX] = cpu_to_be32(GIC_FDT_IRQ_TYPE_SPI);
    irq_attr[3 * STOPWATCH_TIMEOUT_IRQ_IDX + 1] = cpu_to_be32(irq_number);
    irq_attr[3 * STOPWATCH_TIMEOUT_IRQ_IDX + 2] =
                                        cpu_to_be32(GIC_FDT_IRQ_FLAGS_LEVEL_HI);

    qemu_fdt_setprop(fdt, nodename, "interrupts",
                     irq_attr, STOPWATCH_NB_IRQ * 3 * sizeof(uint32_t));
```

And this is it for the hypervisor side. Before looking at the actual
implementation of the stopwatch behavior, let us finish the structural
part with guest lookup of the virtual device resources.

Probing of the device from the guest system
-------------------------------------------

Now that we have a virtual device connected to the virtual machine and
exposed to the guest system (through a device-tree entry), we can
probe it from the guest Linux.

### Device driver probing


```
\\~ driver/stopwatch.c

#define DRIVERNAME "stopwatch"

static int stopwatch_probe(struct platform_device *pdev);
static int stopwatch_remove(struct platform_device *pdev);

static const struct of_device_id stopwatch_match[] = {
    { .compatible = "stopwatch", },
    {}
};

static struct platform_driver stopwatch_driver = {
    .probe = stopwatch_probe,
    .remove = stopwatch_remove,
    .driver = {
        .name = DRIVERNAME,
        .of_match_table = of_match_ptr(stopwatch_match),
    },
};


module_platform_driver_probe(stopwatch_driver, stopwatch_probe);
```

This will trigger the execution of `stopwatch_probe` when a) the
driver is loaded b) the "stopwatch" compatible string is found in a
device tree entry.

### Device tree entry inspection

Now that our driver is being loaded, we need to inspect the device
tree entry and map the memory-mapped IO region into the kernel memory
space, and connect a handler to the IRQ line.

```
\\~ driver/stopwatch.c

struct stopwatch_data {
    struct miscdevice mdev;

    struct StopWatch_regs __iomem *regs_base_addr;
    resource_size_t regs_base_physaddr;
    resource_size_t regs_size;

    struct StopWatch_mem  __iomem *mem_base_addr;
    resource_size_t mem_base_physaddr;
    resource_size_t mem_size;

    ...
} *pdata;

static int stopwatch_probe(struct platform_device *pdev) {
    struct device *dev = &pdev->dev;

```

1. Mapping of the memory region (memory resource 0)

```
    struct resource *mem_res;

    mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    pdata->mem_base_physaddr = mem_res->start;
    pdata->mem_size = mem_res->end - mem_res->start + 1;

    pdata->mem_base_addr = devm_ioremap_resource(dev, mem_res);

    DEBUG_MSG("mapped region 0/mem,  physaddr: 0x%lx, size: 0x%lx",
              (unsigned long) pdata->mem_base_physaddr,
              (unsigned long) pdata->mem_size);
```

2. Mapping of the register region (memory resource 1)

```
    struct resource *regs_res;

    regs_res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
    pdata->regs_base_physaddr = regs_res->start;
    pdata->regs_size = regs_res->end - regs_res->start + 1;
    pdata->regs_base_addr = devm_ioremap_resource(dev, regs_res);
```

2. IRQ registration (IRQ resource 0)

```
    static irqreturn_t timeout_irq_handler(int irq, void *dev_id);
    int timeout_irq_no;

    timeout_irq_no = platform_get_irq(pdev, 0);
    devm_request_irq(dev, timeout_irq_no, timeout_irq_handler, 0,
				     "stopwatch timeout", NULL);
```

And that's it! After this proving, we have the following resources
available to the guest driver:

- `pdata->mem_base_addr`, an MMIO region passthroughed to with Qemu
- `pdata->regs_base_addr`, an MMIO region that traps into Qemu
- `timeout_irq_handler()`, an IRQ handler that will trigger on Qemu
  request

And we are now ready to implement the specific behavior of our
stopwatch device!

Virtual Device and Guest Driver Interactions
--------------------------------------------

In this section, we describe the implementation of the interaction
between the driver in the guest Linux, and the virtual device inside
Qemu.

### Data structures

The interactions between the guest driver and the virtual device
happen over the two memory banks of the virtual device: the register
bank and the memory bank. The structure of these two memory areas is
defined by the data structures below:

```
\\~ driver/stopwatch_hw-sw.h

struct StopWatch_regs {
	uint64_t command;
	uint64_t status;
};

#define STOPWATCH_MEM_DATA_LENGTH 128
struct StopWatch_mem {
	uint64_t data_len;
	char data[STOPWATCH_MEM_DATA_LENGTH];
};
```

The `command` register is SW-to-HW and can take the following values:

```
enum {
	STOPWATCH_ACTION_RESET,  // 0
	STOPWATCH_ACTION_START,  // 1
	STOPWATCH_ACTION_PAUSE,  // 2
	STOPWATCH_ACTION_UPDATE, // 3
	STOPWATCH_ACTION_TIMEOUT, // 4
	STOPWATCH_ACTION_TIMEOUT_ACK, // 5

	STOPWATCH_ACTION_LAST // keep last
};
```

and the `status` register is HW-to-SW and can take the following
values:

```
enum {
	STOPWATCH_STATE_RUNNING, // 0
	STOPWATCH_STATE_RESET,   // 1
	STOPWATCH_STATE_PAUSED,  // 2

	STOPWATCH_STATE_LAST // keep last
};
```

The `memory` bank is composed of a length, and a set of `char`, based
on the commands that have been issued by the driver.

### Driver-side Operations

In the driver, the command are issued with the `writel` function, and
the status is read with the `readl` function:


```
\\~ driver/stopwatch.c
static inline void trigger_cmd(uint64_t cmd)
{
    writel(cmd, &pdata->regs_base_addr->command);
}

static inline uint64_t get_status(void)
{
    return readl(&pdata->regs_base_addr->status);
}
```
and the memory is read and written with the `memcpy_from/toio`
functions, eg:

```
	memcpy_fromio(pdata->data, &pdata->mem_base_addr->data, pdata->data_len);
```

### Device-side Operations

On the virtual device, the reads and writes on the memory banks do not
trigger any callback, nor VM exits. Only the writes on the `command`
register and the reads on the `status` register trigger valid actions:

```
static void stopwatch_io_write(void *opaque, hwaddr offset, uint64_t command,
                               unsigned size)
{
    struct StopWatchState *s = opaque;

    switch(offset) {
    case offsetof(struct StopWatch_regs, command):
        STOPWATCH_PRINT("%s: COMMAND value: 0x%lx\n", __func__, command);
        if (stopwatch_action(s, command)) {
            hw_error("invalid action (%ld)", command);
        }
        break;
        ;;
    default:
        STOPWATCH_PRINT("%s: offset: WRITE at %"HWADDR_PRIx" is INVALID (command: 0x%lx)\n",
                        __func__, offset, command);
        hw_error("invalid write");
        ;;
    }
}

static uint64_t stopwatch_io_read(void *opaque, hwaddr offset,
                                  unsigned size)
{
    struct StopWatchState *s = opaque;
    uint64_t value = 0;

    switch(offset) {
    case offsetof(struct StopWatch_regs, status):
        value = s->status;
        STOPWATCH_PRINT("%s: STATUS: 0x%lx\n", __func__, value);
        break;
        ;;
    default:
        STOPWATCH_PRINT("%s: offset: READ at %"HWADDR_PRIx" is INVALID\n",
                        __func__, offset);
        hw_error("invalid read");
        ;;
    }
    return value;
}
```

The reads on the `status` register is handled right away. The writes
on the `command` register trigger more complicated behaviors, based on
the internal state of the device:


```
static int stopwatch_action(struct StopWatchState *s, uint64_t command) {
    switch(command) {
    ...
    case STOPWATCH_ACTION_PAUSE:
        if (s->status != STOPWATCH_STATE_RUNNING) {
            hw_error("COMMAND pause: Stopwatch wasn't running (%ld)", s->status);
        }
        s->status = STOPWATCH_STATE_PAUSED;
        s->total_time_s += get_running_time(s);
        return 0;
        ;;
    ...
    }
    return 1;
}
```

In the next and last section quickly presents how the interactions
between the userland and the driver happen. This is unrelated to
para-virtualization, but it completes the code overview.

Guest Driver and Guest Userland Interactions
--------------------------------------------
