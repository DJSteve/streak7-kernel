cmd_block/built-in.o :=  arm-eabi-ld -EL    -r -o block/built-in.o block/elevator.o block/blk-core.o block/blk-tag.o block/blk-sysfs.o block/blk-barrier.o block/blk-settings.o block/blk-ioc.o block/blk-map.o block/blk-exec.o block/blk-merge.o block/blk-softirq.o block/blk-timeout.o block/blk-iopoll.o block/blk-lib.o block/ioctl.o block/genhd.o block/scsi_ioctl.o block/noop-iosched.o block/deadline-iosched.o block/cfq-iosched.o block/sio-iosched.o block/bfq-iosched.o block/vr-iosched.o 
