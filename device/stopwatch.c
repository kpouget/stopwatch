#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qemu/timer.h"

#include "sys/mman.h"
#include <stddef.h>

#include "hw/misc/stopwatch.h"
#include "../../driver/stopwatch_hw-sw.h"

static double get_running_time(struct StopWatchState *s) {
    time_t end;

    time(&end);

    return difftime(end, s->started_at);
}

static int stopwatch_action(struct StopWatchState *s, uint64_t command) {
    switch(command) {
    case STOPWATCH_ACTION_RESET:
        STOPWATCH_PRINT("%s: COMMAND reset\n", __func__);

        s->status = STOPWATCH_STATE_RESET;
        s->total_time_s = 0;
        s->started_at = 0;

        return 0;
        ;;
    case STOPWATCH_ACTION_START:
        if (s->status != STOPWATCH_STATE_RESET
            && s->status != STOPWATCH_STATE_PAUSED) {
            hw_error("COMMAND start: Stopwatch isn't in"
                     " reset/paused state (%ld)", s->status);
        }

        STOPWATCH_PRINT("%s: COMMAND start\n", __func__);

        s->status = STOPWATCH_STATE_RUNNING;
        time(&s->started_at);

        return 0;
        ;;
    case STOPWATCH_ACTION_PAUSE:
        if (s->status == STOPWATCH_STATE_PAUSED) {
            STOPWATCH_PRINT("%s: COMMAND pause: already paused\n", __func__);

            return 0;
        }
        if (s->status != STOPWATCH_STATE_RUNNING) {
            hw_error("COMMAND pause: Stopwatch wasn't running (%ld)", s->status);
        }

        STOPWATCH_PRINT("%s: COMMAND pause\n", __func__);

        s->status = STOPWATCH_STATE_PAUSED;

        s->total_time_s += get_running_time(s);

        return 0;
        ;;
    case STOPWATCH_ACTION_UPDATE:
    {
        double time = s->total_time_s;
        if (s->status == STOPWATCH_STATE_RUNNING) {
            time += get_running_time(s);
        }

        snprintf(s->mem_ptr->data, STOPWATCH_MEM_DATA_LENGTH, "%.2lf seconds", time);
        s->mem_ptr->data_len = strlen(s->mem_ptr->data) + 1;

        STOPWATCH_PRINT("%s: COMMAND update: %s (len=%ld)\n", __func__,
                        s->mem_ptr->data, s->mem_ptr->data_len);

        return 0;
        ;;
    }
    case STOPWATCH_ACTION_TIMEOUT:
    {
        uint64_t timeout = s->mem_ptr->data_len;
        if (timeout > STOPWATCH_TIMEOUT_MAX) {
            hw_error("COMMAND timeout: Stopwatch cannot wait more than %ds "
                     "(%lds requested)", STOPWATCH_TIMEOUT_MAX, timeout);
        }
        if (s->timeout_ongoing) {
            STOPWATCH_PRINT("%s: COMMAND timeout: timer already ongoing ...\n",
                            __func__);
            return 0;
        }

        s->timeout_ongoing = true;
        timer_mod(s->timeout_timer,
                  qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + timeout * 1000);

        STOPWATCH_PRINT("%s: COMMAND timeout: %ld-second timer started\n",
                        __func__, timeout);

        return 0;
        ;;
    }
    case STOPWATCH_ACTION_TIMEOUT_ACK:
        if (!s->timeout_ongoing) {
            hw_error("COMMAND timeout_ack: Timeout IRQ not ongoing ...");
        }
        s->timeout_ongoing = false;
        qemu_set_irq(s->timeout_irq, 0);

        STOPWATCH_PRINT("%s: COMMAND timeout ack: IRQ turned off\n", __func__);
        return 0;
        ;;
    default:
        STOPWATCH_PRINT("%s: COMMAND invalid: 0x%lx\n", __func__, command);
        ;;
    }
    return 1;
}

static void stopwatch_timeout_cb(void *opaque) {
    struct StopWatchState *s = opaque;

    STOPWATCH_PRINT("%s: COMMAND timeout: timeout !\n", __func__);
    assert(s->timeout_ongoing);

    qemu_set_irq(s->timeout_irq, 1);
}


static int stopwatch_init(struct StopWatchState *s) {
    stopwatch_action(s, STOPWATCH_ACTION_RESET);

    if (s->start_at_boot) {
        stopwatch_action(s, STOPWATCH_ACTION_START);
    }

    s->timeout_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, stopwatch_timeout_cb, s);
    s->timeout_ongoing = false;

    return 0;
}

/***************************/

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

static const MemoryRegionOps stopwatch_regs_ops = {
    .write = stopwatch_io_write,
    .read = stopwatch_io_read,
    .impl = {
        .min_access_size = 8,
        .max_access_size = 8,
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};


static Property stopwatch_properties[] = {
    DEFINE_PROP_BOOL("start_at_boot", struct StopWatchState, start_at_boot,
                     true),
    DEFINE_PROP_END_OF_LIST(),
};

#define STOPWATCH_IO_REGS_SIZE (sizeof(struct StopWatch_regs))
#define STOPWATCH_IO_MEM_SIZE (sizeof(struct StopWatch_mem))

static void stopwatch_realize(DeviceState *dev, Error **errp)
{
    struct StopWatchState *s = STOPWATCH(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    /*
     * Instanciation of the virtual device
     */

    s->mem_ptr = mmap(0, STOPWATCH_IO_MEM_SIZE, PROT_READ|PROT_WRITE,
                      MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (s->mem_ptr == MAP_FAILED) {
        error_setg_errno(errp, errno, "Unable to allocate the stopwatch memory");
        return;
    }

    memory_region_init_ram_ptr(&s->mem, OBJECT(s), TYPE_STOPWATCH"-mem",
                               STOPWATCH_IO_MEM_SIZE, s->mem_ptr);

    memory_region_init_io(&s->regs, OBJECT(s), &stopwatch_regs_ops, s,
                          TYPE_STOPWATCH"-regs", STOPWATCH_IO_REGS_SIZE);


    sysbus_init_mmio(sbd, &s->mem);
    sysbus_init_mmio(sbd, &s->regs);
    sysbus_init_irq(sbd, &s->timeout_irq);

    stopwatch_init(s);
}

static void stopwatch_unrealize(DeviceState *dev, Error **errp)
{
    struct StopWatchState*s = STOPWATCH(dev);

    munmap(s->mem_ptr, STOPWATCH_IO_MEM_SIZE);
}

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

#include "hw/platform-bus.h"
#include "sysemu/device_tree.h"
#include "hw/arm/fdt.h"

/*
 * internal struct that contains the information to create dynamic
 * sysbus device node
 */
typedef struct PlatformBusFDTData { /* WARN: copied from hw/arm/sysbus-fdt.c */
    void *fdt; /* device tree handle */
    int irq_start; /* index of the first IRQ usable by platform bus devices */
    const char *pbus_node_name; /* name of the platform bus node */
    PlatformBusDevice *pbus;
} PlatformBusFDTData;

/**
 * add_stopwatch_fdt_node
 *
 * Add simple node for the StopWatch device.
 */
int add_stopwatch_fdt_node(SysBusDevice *sbdev, void *opaque)
{
    PlatformBusFDTData *data = opaque;
    PlatformBusDevice *pbus = data->pbus;
    struct StopWatchState *stopwatch_state = STOPWATCH(sbdev);
    void *fdt = data->fdt;
    const char *parent_node = data->pbus_node_name;
    int ret = -1;
    char *nodename;
    uint32_t *irq_attr, *reg_attr;
    uint64_t mmio_base, irq_number;

    const int NB_MEM_REGION = 2;

    mmio_base = platform_bus_get_mmio_addr(pbus, sbdev, 0);
    nodename = g_strdup_printf("%s/%s@%" PRIx64, parent_node,
                               TYPE_STOPWATCH, mmio_base);
    qemu_fdt_add_subnode(fdt, nodename);

    qemu_fdt_setprop(fdt, nodename, "compatible", STOPWATCH_COMPAT_STR,
                     sizeof(STOPWATCH_COMPAT_STR));

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
    if (ret) {
        error_report("could not set reg property of node %s", nodename);
        goto fail;
    }

#define STOPWATCH_NB_IRQ 1
#define STOPWATCH_TIMEOUT_IRQ_IDX 0

    irq_attr = g_new(uint32_t, STOPWATCH_NB_IRQ * 3);
    irq_number = platform_bus_get_irqn(pbus, sbdev ,STOPWATCH_TIMEOUT_IRQ_IDX)
        + data->irq_start;
    error_report("Register IRQ #%u for Stopwatch device",
                 (unsigned int) irq_number);

    irq_attr[3 * STOPWATCH_TIMEOUT_IRQ_IDX] = cpu_to_be32(GIC_FDT_IRQ_TYPE_SPI);
    irq_attr[3 * STOPWATCH_TIMEOUT_IRQ_IDX + 1] = cpu_to_be32(irq_number);
    irq_attr[3 * STOPWATCH_TIMEOUT_IRQ_IDX + 2] =
                                        cpu_to_be32(GIC_FDT_IRQ_FLAGS_LEVEL_HI);

    qemu_fdt_setprop(fdt, nodename, "interrupts",
                     irq_attr, STOPWATCH_NB_IRQ * 3 * sizeof(uint32_t));
fail:
    g_free(nodename);
    g_free(reg_attr);
    return 0;
}
