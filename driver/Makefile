all: modules

ARCH=arm64
CROSS_COMPILE=aarch64-linux-gnu-
KSRCDIR=../linux
KBUILDDIR=${KSRCDIR}/build

###

obj-m += stopwatch.o
CFLAGS_stopwatch.o :=
CFLAGS_stopwatch.mod.o := ${CFLAGS_stopwatch.o}

modules:
	make -C $(KSRCDIR) M=$(PWD) O=${KBUILDDIR} CROSS_COMPILE=${CROSS_COMPILE}  ARCH=${ARCH} modules

clean:
	make -C ${KSRCDIR} M=$(PWD) clean
