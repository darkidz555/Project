#
# Makefile for the kernel block layer
#

obj-$(CONFIG_BLOCK) := bio.o elevator.o blk-core.o blk-tag.o blk-sysfs.o \
			blk-flush.o blk-settings.o blk-ioc.o blk-map.o \
			blk-exec.o blk-merge.o blk-softirq.o blk-timeout.o \
			blk-iopoll.o blk-lib.o blk-mq.o blk-mq-tag.o \
			blk-mq-sysfs.o blk-mq-cpu.o blk-mq-cpumap.o ioctl.o \
			genhd.o scsi_ioctl.o partition-generic.o ioprio.o \
			partitions/

obj-$(CONFIG_IOSCHED_NOOP)	+= noop-iosched.o
obj-$(CONFIG_IOSCHED_DEADLINE)	+= deadline-iosched.o
obj-$(CONFIG_IOSCHED_CFQ)	+= cfq-iosched.o
obj-$(CONFIG_IOSCHED_FIOPS)     += fiops-iosched.o
obj-$(CONFIG_IOSCHED_SIO)       += sio-iosched.o
obj-$(CONFIG_IOSCHED_ZEN)	+= zen-iosched.o
obj-$(CONFIG_IOSCHED_BFQ)	+= bfq-iosched.o
obj-$(CONFIG_IOSCHED_ANXIETY)	+= anxiety-iosched.o



