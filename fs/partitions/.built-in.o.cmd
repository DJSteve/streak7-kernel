cmd_fs/partitions/built-in.o :=  arm-eabi-ld -EL    -r -o fs/partitions/built-in.o fs/partitions/check.o fs/partitions/msdos.o fs/partitions/efi.o 
