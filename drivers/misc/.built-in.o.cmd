cmd_drivers/misc/built-in.o :=  arm-eabi-ld -EL    -r -o drivers/misc/built-in.o drivers/misc/kernel_debugger.o drivers/misc/uid_stat.o drivers/misc/sensors_daemon.o drivers/misc/current_driving.o drivers/misc/eeprom/built-in.o drivers/misc/cb710/built-in.o drivers/misc/bcm4329_rfkill.o drivers/misc/nct1008.o drivers/misc/tegra-cryptodev.o 
