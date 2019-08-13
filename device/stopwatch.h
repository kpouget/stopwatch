#ifndef HW_MISC_STOPWATCH_H
#define HW_MISC_STOPWATCH_H

#define TYPE_STOPWATCH            "stopwatch"

#define DEBUG_STOPWATCH
#ifdef DEBUG_STOPWATCH
#define STOPWATCH_PRINT(fmt, ...) \
    do {fprintf(stderr, TYPE_STOPWATCH " - " fmt, ## __VA_ARGS__);      \
        fflush(stdout);                                                 \
    } while (0)
#else
#define STOPWATCH_PRINT(fmt, ...)
#endif

#define STOPWATCH_COMPAT_STR TYPE_STOPWATCH /* Device Tree compatible string */

#define STOPWATCH(obj) \
                OBJECT_CHECK(struct StopWatchState, (obj), TYPE_STOPWATCH)

struct StopWatchState {
    /*< private >*/
    SysBusDevice dev;

    /*< public >*/
    MemoryRegion regs;

    MemoryRegion mem;

    qemu_irq timeout_irq;

    /*< properties >*/

    bool start_at_boot;

    /*< internal state >*/

    struct StopWatch_mem *mem_ptr;

    uint64_t status;
    time_t started_at;
    double total_time_s;

    QEMUTimer *timeout_timer;
    bool timeout_ongoing;
};

struct StopWatchDeviceClass {
    /*< private >*/
    SysBusDeviceClass parent_class;
    /*< public >*/
};

int add_stopwatch_fdt_node(SysBusDevice *sbdev, void *opaque);

#endif
